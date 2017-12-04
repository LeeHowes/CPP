#include "MyAsyncLibrary.h"

#include "Executor.h"
#include <thread>

void where(std::string name) {
    std::cout << name << "; On thread: " << std::this_thread::get_id() << "\n";
}

namespace MyLibrary {
namespace {
std::shared_ptr<DrivenExecutor> globalExecutor;
std::thread libraryWorkerThread;
}


void init() {
    globalExecutor = std::make_shared<DrivenExecutor>();
    libraryWorkerThread = std::thread([&](){
        where("Helper thread start");
        globalExecutor->run();
        where("Helper thread end");
      });

}

void shutdown() {
    globalExecutor->terminate();
    libraryWorkerThread.join();
}

std::shared_ptr<DrivenExecutor> getExecutor() {
    return globalExecutor;
}

} // namespace MyLibrary
