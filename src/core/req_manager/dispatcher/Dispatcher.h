#pragma once

class ReqManager;

class DispatcherWorker {
public:
    explicit DispatcherWorker(ReqManager &manager);
    void Run();

private:
    ReqManager &manager_;
};
