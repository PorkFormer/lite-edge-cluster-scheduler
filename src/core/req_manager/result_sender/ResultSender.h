#pragma once

class ReqManager;

class ResultSenderWorker {
public:
    explicit ResultSenderWorker(ReqManager &manager);
    void Run();

private:
    ReqManager &manager_;
};
