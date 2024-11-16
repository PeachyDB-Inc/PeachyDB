## Performance Monitoring

In order to figure out any performance related issues, you can obtain a performance bundle from the specific servers. There you will find a file perf.peachdb.messglog. This file contains some statistics on the database operation. Below is the explanation of numerous counters available. When collecting stats from any of the servers note that the stats are reported every 10 minutes (as of this writing, but you easily check from the log file if it has been made more frequent), so there is not much to be gained by collecting stats more frequently and it will only add additional burden on the servers to keep returning these files to the user.

## Database Buffer Pool Stats

Database Cache stats can be seen in lines of the following form in the performance log:

Cache Stats:<br>
num-hits=%ld num-misses=%ld<br>
num-evictions=%ld num-dirty-page-evictions=%ld num-admission-evictions=%ld num-admission-dirty-evictions=%ld num-sync-io=%ld<br>
num-async-iowaits=%ld num-hash-gcent=%ld num-hash-delent=%ld num-hash-chain-steps=%ld num-forced-drains=%ld<br>
num-prefetched-pages=%ld num-trickle-calls=%ld num-trickle-writes=%ldnum-trickle-syncs=%ld num-dirtyring-drains=%ld<br>
num-commit-writes=%ld num-commit-syncs=%ld


### Explanation of Counters:

- **num-evictions**: Number of buffers evicted from the buffer cache.

- **num-dirty-page-evictions**: Number of buffers evicted that were dirty.

- **num-admission-evictions**: Number of buffers evicted from admission cache.

- **num-admission-dirty-evictions**: Number of dirty buffers evicted from admission cache.

- **num-sync-io**: Number of times an execution thread waited for an i/o issued by itself.

- **num-async-iowaits**: Number of times an execution thread waited for i/o issued by a different thread.

- **num-forced-drains**: Internal buffer ring full and forced to be drained inline with execution thread. Note that this different from num-dirtyring-drains. It should not happen much at all in a normally running database server.

- **num-prefetched-pages**: Number of pages prefetched by prefetching threads. If prefetching is working properly, it should reduce both `num-sync-io` and `num-async-iowaits`. The goal of prefetching pages is to avoid having running transactions run into i/o waits for pages.

- **num-trickle-calls**: Number of times trickle threads were executed.

- **num-trickle-writes**: Number of buffers written out to OS buffer pool. This should be correlated to `num-commit-writes`, so that the latter counter is 0 or grows very slowly.

- **num-trickle-syncs**: Number of times `fsync()` was issued by trickle thread. This should be correlated to `num-commit-syncs`, so that the latter counter is 0 or grows very slowly.

- **num-dirtyring-drains**: Dirty buffer ring full and forced to be drained inline with execution thread. This should ideally be 0, as it is the goal of the trickle threads to keep draining the dirty buffers to the OS buffer pool so that an executor thread is never forced to drain dirty buffer ring inline, thereby incurring the cost of i/o to the running transaction.

- **num-commit-writes**: Number of dirty buffers written to OS buffer pool by writer at commit. This should be very close to 0, as ideally the trickle threads would push out the dirty buffers from the database cache.

- **num-commit-syncs**: Number of times `fsync()` was issued on data file during commit. This should be very close to 0, as ideally all the `fsyncs` needed should be performed by the trickle threads.

## Write Transaction Stats

If you see a line of the following form in the performance log:

TID:%d WRITE debug-counts num-wrqs=%ld flushwr=%d exec-call=%ld%% rpc-wait=%ld%% rpc-total=%ld%% rpc-callback=%ld%%


### Explanation of Terms:

- **TID**: Writer thread id.
- **num-wrqs**: Number of write transactions issued.

### Breakdown of Execution Time:

- **exec-calls**: Percentage of time spent in execution.
  - `exec-call = rpc-wait + rpc-total`
- **rpc-wait**: Percentage of time spent waiting on distributed consensus. This should be as low as possible. Typically, if the transactions are CPU bound and there are enough outstanding writes, `rpc-wait` is low. To reduce `rpc-wait`, increase the number of client threads/fibers issuing write transactions, as long as there are no write timeouts. This metric is crucial to determine if enough write client threads are being utilized. This is the most important metric to determine if the CPU is being fully utilized for writes or its waiting on consensus. Overburdening a weak EC2 Client Instance with too many threads might indicate performance issues on the client as well.
- **rpc-total**: Percentage of time that was spent in the execution.
- **rpc-callback**: Percentage of time spent in execution callback.

If you see the line of the following form immediately after the line above:

TID:%d p:%ld%% e:%ld%% s:%ld%% c:%ld%%

It Represents:

- **TID**: Writer thread id.
- **p**: Parse time.
- **e**: Exec time.
- **s**: Serialize time.
- **c**: Commit time.

In these stats, the **commit time** is the most crucial figure. For a write transaction, commit time should not significantly increase over a period. If commit time is growing, it may indicate issues with trickle frequency tuning and bugs in write commit overhead management. The statistics are reported in performance log every 10 minutes and the percentages of CPU utilization are for the 10 minute window, if the servers were idle for some part of the window then the percentages will indicate lower CPU utilization. Please read note below about issue #18.

## Read Transaction Stats

If you see a line of the following form in the performance log:

TID:%d READ debug-counts num-rdqs=%ld num-ro-cons=%ld num-wr-cons=%ld exec-call=%d%% relay=%ld%% ro-resp=%ld%% wr-resp=%ld%% rpc-callback=%ld%%


### Explanation of Terms:

- **TID**: Reader thread id.
- **num-rdqs**: Number of read transactions executed by the reader thread.
- **num-ro-cons**: Number of distributed read-only query result message consolidations performed by this thread.
- **num-wr-cons**: Number of distributed write query result message consolidations performed by this thread. Yes, it is possible for the consolidation of results task of a write transaction to be undertaken by a reader thread.

### Breakdown of Execution Time:

- **exec-call**: (Explanation needed)
- **relay**: (Explanation needed)
- **ro-resp**: (Explanation needed)
- **wr-resp**: (Explanation needed)
- **rpc-callback**: (Explanation needed)

If you see the following line immediately after the line above:

TID:%d p:%ld%% e:%ld%% s:%ld%% c:%ld%%

It Represents:

- **TID**: Reader thread id.
- **p**: Parse time.
- **e**: Exec time.
- **s**: Serialize time.
- **c**: Commit time.

Typically, for a reader thread, these stats are not of much interest but have been provided just in case there are any anomalies due to bugs. The statistics are reported in performance log every 10 minutes and the percentages of CPU utilization are for the 10 minute window, if the servers were idle for some part of the window then the percentages will indicate lower CPU utilization. Please read note below about issue #18.

## Write Consolidator Stats

Separate threads can be dedicated to the consolidation of distributed write transaction responses.

If you see a line of the following form in the performance log:

TID:%d CONSOLIDATOR debug-counts num-mssgs=%ld exec=%ld%% wr=%ld%%


It Represents:

- **TID**: Consolidator thread id.
- **num-mssgs**: Number of messages/packets handled by the consolidator thread.
- **exec**: (Explanation needed)
- **wr**: (Explanation needed)

The statistics are reported in performance log every 10 minutes and the percentages of CPU utilization are for the 10 minute window, if the servers were idle for some part of the window then the percentages will indicate lower CPU utilization. Please read note below about issue #18.

## Client Performance
- The client is multi-threaded.
- During testing phase, utilize 'top' in a separate shell to monitor CPU utilization on the client machine. Or you can also utilize mpstat.
- If CPU is 100% or close, it may be because the client is spending a lot of time preparing/disgesting queries. If queries are fixed and known
 in advance (with possibly different arguments passed on each invocation) then the query statements can be prepared once outside the execution loop and
 thereafter be reset prior to each invocation to provide different parameters/arguments for the next execution. Client driver examples provide explanation of how this is done.
- Having too many threads on a single client can also cause too many context switches between the threads and the overall CPU utilization may not
improve much.
- If the client still has high CPU utilization or the additional number of threads/fibers doesn't appear to improve query throughput on the client, it
may be better to add additional AWS client instances or stronger instances to drive more load to under utilized database servers. Please confirm from the server stats that the servers still have untilized CPU cycles and adding more client threads can help.
- By default, if the client has n vCPUs then the client will run n threads with 8 fibers per thread. Having too many fibers is not likely to help if each client thread is spending a lot of time in query parsing or in other computationaly intensive tasks.
It has been observed that if the client threads are doing too much work then having fewer fibers per thread and having more AWS client instances can improve server CPU utilization.
- If the CPUs on the client are underutilized, even after increasing threads/fibers, as determined by mpstat, most likely this is because the fibers/threads are spending that time waiting on response packets from the servers rather than doing any processing. In this case there may not be enough processing power available on the servers.
- Boost c++ fibers perform better than threads for client driver so long as co-operative scheduling is respected. A quick review of this can be obtained at <a href="https://boost.org/doc/libs/1_85_0/libs/fiber/doc/html/index.html">boost c++ fibers </a>. It requires understanding of a few basic concepts which can be useful regardless of our product. If you are unable to utilize Boost fibers, then threads can be utilized.
- __IMPORTANT:__ When figuring out the %CPU utilization from the server stats please note that the statistics reported are avergaes per 10 minute time window. If in this time window the CPU was idle 50% of the time, and later subjected to a lot of queries then the CPU will report 50% CPU utilization. Thus when figuring out CPU utilization it is important to make certain that the client was sending load during the whole time window. This can often imply that the very first reading of statistics is to be ignored because for some part of that time window the servers were idle. Please refer to Issue [18](https://github.com/akseg73/PeachyDB/issues/18).

### Tuning Client Threads
throughput = parallelism/latency

- If latency of query response increases due to database page i/o, then you may have to increase the number of client threads to improve
 throughput. This can help so long as the database storage offers improved performance with greater parallelism. Such issues exist in other
 databases as well, when i/o becomes a prime factor in throughput.
- In performance statistics look for "Cache Stats::", if you find that there are a lot of cache misses and there are a lot of waits on i/o pages as well we prefetching of pages, this indicates that i/o is playing a role in latency.
- One way to deal with this is to keep a pool of client threads/fibers and depending upon the latency observed potentially increase the number
 of active fibers if the other performance counters indicate that the CPU is idling while the queries are waiting on i/o. As the database far
 exceeds the size of available RAM, latency is bound to be impacted due to storage i/o. This is why as data gets loaded the write throughput
 will degrade linearly unless more parallelism is introduced.





