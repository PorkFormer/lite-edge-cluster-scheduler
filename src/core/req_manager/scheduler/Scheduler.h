#pragma once

class ReqManager;

class SchedulerWorker {
public:
    explicit SchedulerWorker(ReqManager &manager);
    void Run();

private:
    ReqManager &manager_;
};
