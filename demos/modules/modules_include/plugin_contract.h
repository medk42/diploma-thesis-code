#pragma once
#include <stdint.h>

#if defined(_WIN32)
  #define DLL_API extern "C" __declspec(dllexport)
#else
  #define DLL_API extern "C" __attribute__((visibility("default")))
#endif

// struct MessageHeader               // travels through the bus
// {
//     uint64_t id, timestamp_ns;
//     uint32_t smallSize;
//     uint32_t blobCount;            // uint32_t blobIds[blobCount] follow
// };

// struct ParameterDesc
// {
//     const char* key;               // "gain_db"
//     uint8_t     type;              // enum ParamType { Int, Double, String, Blob }
//     const char* defaultValue;
// };

// struct ParameterList
// {
//     uint32_t           count;
//     const ParameterDesc* items;    // DLL-owned
// };

// struct CoreAPI
// {
//     void (*publish)(const MessageHeader*, const void* small, const uint32_t* blobs);
//     uint32_t (*allocBlob)(uint32_t bytes);      // returns blobId
//     void (*freeBlob)(uint32_t blobId);          // ref-counted
//     void (*log)(int level, const char* txt);    // 0=dbg,1=inf,2=err
// };

// class IModule
// {
// public:
//     virtual void process(const MessageHeader& h,
//                          const void* small,
//                          const uint32_t* blobs) noexcept = 0;
//     virtual void release() noexcept = 0;
// protected:
//     virtual ~IModule() = default;
// };

// ---- mandatory C exports ------------------------------------------
// DLL_API int        GetPluginAPIVersion();                 // e.g. 1
// DLL_API void       GetRequiredParameters(ParameterList*);
// DLL_API bool       SetProvidedParameters(const ParameterList*);
// DLL_API IModule*   CreateModule(const CoreAPI*) noexcept; // new Wrapper

DLL_API const char* readPluginName();
DLL_API uint64_t readPluginApiVersion();