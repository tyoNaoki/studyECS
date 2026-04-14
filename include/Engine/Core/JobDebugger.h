#pragma once
#include "TestFramework.hpp"
#include <optional>

namespace ECS::JobSystem::Debug {

    class JobDebugMixin
    {
    protected:
        static constexpr auto MAX_EMPTY_DURATION = std::chrono::milliseconds(10000);

        bool checkStuck() {
            auto now = std::chrono::steady_clock::now();
            if (now - *firstEmptyTime > MAX_EMPTY_DURATION) {
                return true;
            }
            return false;
        }

        void clearTimer() {
            firstEmptyTime.reset();
        }

        void resetTimer() {
            firstEmptyTime = std::chrono::steady_clock::now();
        }

    private:
        std::optional<std::chrono::steady_clock::time_point> firstEmptyTime;
    };

    template <typename BaseQueue>
    class DebugJobQueue
        : public BaseQueue
        , private ECS::JobSystem::Debug::JobDebugMixin
    {
    public:
        using Base = BaseQueue;
        using PushResult = typename Base::PushResult;
        using PopResult = typename Base::PopResult;
        using StealResult = typename Base::StealResult;

        // BaseQueue のコンストラクタをそのまま使えるように
        template <typename... Args>
        DebugJobQueue(Args&&... args)
            : BaseQueue(std::forward<Args>(args)...),isStuck(false)
        {
            clearTimer();
            resetTimer();
        }

        PopResult popBottom() {
            auto res = BaseQueue::popBottom();

            /*if (res.status == PopStatus::Success) {
                resetTimer();
            }
            else {
                if(checkStuck()){
                    if(!isStuck) {
                        std::lock_guard<std::mutex>sk(stuckMutex);

                        if (isStuck) return res;
                        isStuck = true;

                        reportStuck();
                        this->validCheck();
                        this->setAbort();
                        return { PopStatus::WouldBlock,{} };
                    }
                    
                }
            }*/
            return res;
        }

        StealResult stealTop(size_t index) {
            auto res = BaseQueue::stealTop(index);

            /*if (res.status == StealStatus::Success) {
                resetTimer();
            }
            else {

                if (checkStuck()) {
                    if (!isStuck) {
                        std::lock_guard<std::mutex>sk(stuckMutex);
                        if(isStuck) return res;
                        isStuck = true;

                        reportStuck();
                        this->validCheck();
                        this->setAbort();
                        return res;
                    }
                }
            }*/

            return res;
        }

        void reportStuck(){
            printf("JobQueue[%zu] STUCK!!\n", this->getQueueIndex());
            test::saveLog("JobQueue[%zu] STUCK!!",this->getQueueIndex());
        }

    private:
        bool isStuck = false;
        std::mutex stuckMutex;
    };
}
