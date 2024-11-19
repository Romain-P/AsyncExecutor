#### Sample

Problem / How to call blocking calls from a rendering thread without impacting fps  
Solution: AsyncExecutor ftw

```cpp
#include "beauty/beauty.hpp" //https://github.com/dfleury2/beauty


//simulate a game loop that requires http requests & get result
int main() { 
    AsyncExecutor async;

	//let's assume the requests are sent from the while. didn't add it on purpose for benchmarking
    for (int i=0; i < 1; ++i) {
        async.execute<beauty::response>([] {
            beauty::client client;

            // Request an URL
            auto [ec, response] = client.get("https://pastebin.com/raw/Q6n031Cn");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            return response;
        });
    }

	//simulate game thread
    while(true) {
        auto now = std::chrono::duration_cast<std::chrono::nanoseconds >(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        auto result = async.try_pop<beauty::response>();

        if (result != nullptr) {
            bool x = result->result->is_status_ok();
            auto after = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            auto diff = after - now;
            printf("atomic read + lock acquire : time diff : %lld\n", diff);
        }
    }
}
```
 
try_pop consumes ~500/1100 ns per frame, which is far decent. Also note that it's not required to poll task every frame, every 200ms (5 frames) sounds reasonnable

##### TODO / LIMITATIONS

Currently, cancel one specific or all tasks does not actually kill the thread, tasks will continue on their separate thread, but the results won't be pushed.  
I did that on purpose: the external libs that's im using for my project are completely blocking without any option to cancel a task (I/O or crypto alg.).  

If you actually need to stop the thread, you can give a shot to the risky TerminateThread api of windows, or add a new atomic integer `CancelRequested` and passes it to the task so it can cancel on demand (if you have the hand on it).

#### Enhancements

Well, this tiny class was written for my special need, so of course it's not the best. A regular thread pool would give a more stateful executor.  
I actually couldn't find a thread pool impl that completely fulfill my need, so I did one ;) 
If thread count or thread caching is a need for you, feel free to PR
