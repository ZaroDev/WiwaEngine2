#pragma once
#include "rhi_common.h"

#include <span>

namespace wiwa
{
    enum class QueueType : uint8_t 
    { 
        GRAPHICS,
        COMPUTE, 
        TRANSFER
    };

    class IFence;
    class ICommandList;

    class IQueue 
    {
    public:
        virtual ~IQueue() = default;
        virtual QueueType get_type() const = 0;
        // Returns the fence value that signals when this submission has finished.
        virtual uint64_t submit(std::span<ICommandList* const> lists) = 0;
        virtual void     wait_for_idle() = 0;
        virtual IFence*  get_fence() = 0;
    };
}