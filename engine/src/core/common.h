#pragma once
#include <memory>

#define BIT(x) (1 << x)

#define ENUM_CLASS_FLAG_OPERATORS(EnumType)                                 \
    inline EnumType operator|(EnumType a, EnumType b)                      \
    {                                                                     \
        return static_cast<EnumType>(                                     \
            static_cast<std::underlying_type<EnumType>::type>(a) |       \
            static_cast<std::underlying_type<EnumType>::type>(b));       \
    }                                                                     \
                                                                          \
    inline EnumType &operator|=(EnumType &a, EnumType b)                 \
    {                                                                     \
        a = a | b;                                                       \
        return a;                                                        \
    }                                                                     \
                                                                          \
    inline EnumType operator&(EnumType a, EnumType b)                    \
    {                                                                     \
        return static_cast<EnumType>(                                     \
            static_cast<std::underlying_type<EnumType>::type>(a) &       \
            static_cast<std::underlying_type<EnumType>::type>(b));       \
    }                                                                     \
                                                                          \
    inline EnumType &operator&=(EnumType &a, EnumType b)                 \
    {                                                                     \
        a = a & b;                                                       \
        return a;                                                        \
    }                                                                     \
                                                                          \
    inline EnumType operator~(EnumType a)                                \
    {                                                                     \
        return static_cast<EnumType>(                                     \
            ~static_cast<std::underlying_type<EnumType>::type>(a));      \
    }                                                                     \
                                                                            \
    inline bool any(EnumType value, EnumType flag)                          \
    {                                                                     \
        using UnderlyingType = std::underlying_type<EnumType>::type;     \
        return (static_cast<UnderlyingType>(value) &                    \
                static_cast<UnderlyingType>(flag)) != 0;                 \
    }

#if WI_DEBUG
#define WI_BUILD_CONFIGURATION "Debug"
#elif WI_RELEASE
#define WI_BUILD_CONFIGURATION "Release"
#elif WI_SHIPPING
#define WI_BUILD_CONFIGURATION "Shipping"
#endif


template<typename T>
using Scope = std::unique_ptr<T>;
template<typename T, typename ... Args>
constexpr Scope<T> CreateScope(Args&& ... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}
template<typename T>
using Ref = std::shared_ptr<T>;
template<typename T, typename ... Args>
constexpr Ref<T> CreateRef(Args&& ... args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template<typename C, typename P>
constexpr Ref<C> DynCast(Ref<P> ref)
{
    return std::dynamic_pointer_cast<C>(ref);
}

template<typename C, typename P>
constexpr Ref<C> Cast(Ref<P> ref)
{
    return std::static_pointer_cast<C>(ref);
}
