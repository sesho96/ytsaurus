#pragma once

#include "public.h"

#include <yt/yt/core/actions/public.h>

#include <yt/yt/core/ytree/yson_struct.h>
#include <yt/yt/core/yson/public.h>

#include <yt/yt/core/misc/property.h>

#include <library/cpp/yt/threading/spin_lock.h>

#include <vector>

namespace NYT::NCGroup {

////////////////////////////////////////////////////////////////////////////////

void RemoveAllSubcgroups(const TString& path);

void RunKiller(const TString& processGroupPath);

////////////////////////////////////////////////////////////////////////////////

struct TKillProcessGroupTool
{
    void operator()(const TString& processGroupPath) const;
};

////////////////////////////////////////////////////////////////////////////////

class TNonOwningCGroup
    : private TNonCopyable
{
public:
    DEFINE_BYREF_RO_PROPERTY(TString, FullPath);

public:
    TNonOwningCGroup() = default;
    explicit TNonOwningCGroup(const TString& fullPath);
    TNonOwningCGroup(const TString& type, const TString& name);
    TNonOwningCGroup(TNonOwningCGroup&& other);

    void AddTask(int pid) const;
    void AddCurrentTask() const;

    bool IsRoot() const;
    bool IsNull() const;
    bool Exists() const;

    std::vector<int> GetProcesses() const;
    std::vector<int> GetTasks() const;
    const TString& GetFullPath() const;

    std::vector<TNonOwningCGroup> GetChildren() const;

    void EnsureExistance() const;

    void Lock() const;
    void Unlock() const;

    void Kill() const;

    void RemoveAllSubcgroups() const;
    void RemoveRecursive() const;

protected:
    TString Get(const TString& name) const;
    void Set(const TString& name, const TString& value) const;
    void Append(const TString& name, const TString& value) const;

    void DoLock() const;
    void DoUnlock() const;

    bool TryUnlock() const;

    void DoKill() const;

    void DoRemove() const;

    void Traverse(
        const TCallback<void(const TNonOwningCGroup&)>& preorderAction,
        const TCallback<void(const TNonOwningCGroup&)>& postorderAction) const;

    TString GetPath(const TString& filename) const;
};

////////////////////////////////////////////////////////////////////////////////

class TCGroup
    : public TNonOwningCGroup
{
protected:
    TCGroup(const TString& type, const TString& name);
    TCGroup(TNonOwningCGroup&& other);
    TCGroup(TCGroup&& other);

public:
    ~TCGroup();

    void Create();
    void Destroy();

    bool IsCreated() const;

private:
    bool Created_ = false;

};

////////////////////////////////////////////////////////////////////////////////

class TCpuAccounting
    : public TCGroup
{
public:
    static const TString Name;

    struct TStatistics
    {
        TDuration UserTime;
        TDuration SystemTime;
        TDuration WaitTime;
        TDuration ThrottledTime;
        ui64 ContextSwitches = 0;
        ui64 PeakThreadCount = 0;
    };

    explicit TCpuAccounting(const TString& name);

    TStatistics GetStatisticsRecursive() const;
    TStatistics GetStatistics() const;

private:
    explicit TCpuAccounting(TNonOwningCGroup&& nonOwningCGroup);
};

void Serialize(const TCpuAccounting::TStatistics& statistics, NYson::IYsonConsumer* consumer);

////////////////////////////////////////////////////////////////////////////////

class TCpu
    : public TCGroup
{
public:
    static const TString Name;

    explicit TCpu(const TString& name);

    void SetShare(double share);
};

////////////////////////////////////////////////////////////////////////////////

class TBlockIO
    : public TCGroup
{
public:
    static const TString Name;

    struct TStatistics
    {
        ui64 BytesRead = 0;
        ui64 BytesWritten = 0;
        ui64 IORead = 0;
        ui64 IOWrite = 0;
        ui64 IOTotal = 0;
    };

    struct TStatisticsItem
    {
        TString DeviceId;
        TString Type;
        ui64 Value = 0;
    };

    explicit TBlockIO(const TString& name);

    TStatistics GetStatistics() const;
    void ThrottleOperations(i64 iops) const;

private:
    //! Guards device ids.
    YT_DECLARE_SPIN_LOCK(NThreading::TSpinLock, SpinLock_);
    //! Set of all seen device ids.
    mutable THashSet<TString> DeviceIds_;

    std::vector<TBlockIO::TStatisticsItem> GetDetailedStatistics(const char* filename) const;

    std::vector<TStatisticsItem> GetIOServiceBytes() const;
    std::vector<TStatisticsItem> GetIOServiced() const;
};

void Serialize(const TBlockIO::TStatistics& statistics, NYson::IYsonConsumer* consumer);

////////////////////////////////////////////////////////////////////////////////

class TMemory
    : public TCGroup
{
public:
    static const TString Name;

    struct TStatistics
    {
        ui64 Rss = 0;
        ui64 MappedFile = 0;
        ui64 MajorPageFaults = 0;
    };

    explicit TMemory(const TString& name);

    TStatistics GetStatistics() const;
    i64 GetMaxMemoryUsage() const;

    void SetLimitInBytes(i64 bytes) const;

    void ForceEmpty() const;
};

void Serialize(const TMemory::TStatistics& statistics, NYson::IYsonConsumer* consumer);

////////////////////////////////////////////////////////////////////////////////

class TNetwork
{
public:
    struct TStatistics
        : public NYTree::TYsonStructLite
    {
        ui64 TxBytes;
        ui64 TxPackets;
        ui64 TxDrops;

        ui64 RxBytes;
        ui64 RxPackets;
        ui64 RxDrops;

        TStatistics(
            ui64 txBytes,
            ui64 txPackets,
            ui64 txDrops,
            ui64 rxBytes,
            ui64 rxPackets,
            ui64 rxDrops)
            : TxBytes(txBytes)
            , TxPackets(txPackets)
            , TxDrops(txDrops)
            , RxBytes(rxBytes)
            , RxPackets(rxPackets)
            , RxDrops(rxDrops)
        { }

        REGISTER_YSON_STRUCT(TStatistics);

        static void Register(TRegistrar registrar);
    };
};

////////////////////////////////////////////////////////////////////////////////

class TFreezer
    : public TCGroup
{
public:
    static const TString Name;

    explicit TFreezer(const TString& name);

    TString GetState() const;
    void Freeze() const;
    void Unfreeze() const;
};

////////////////////////////////////////////////////////////////////////////////

std::map<TString, TString> ParseProcessCGroups(const TString& str);
std::map<TString, TString> GetProcessCGroups(pid_t pid);
bool IsValidCGroupType(const TString& type);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCGroup
