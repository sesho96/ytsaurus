#pragma once

#include "public.h"

#include <core/concurrency/throughput_throttler.h>

#include <ytlib/chunk_client/config.h>

#include <server/hydra/config.h>

namespace NYT {
namespace NDataNode {

////////////////////////////////////////////////////////////////////////////////

class TPeerBlockTableConfig
    : public NYTree::TYsonSerializable
{
public:
    int MaxPeersPerBlock;
    TDuration SweepPeriod;

    TPeerBlockTableConfig()
    {
        RegisterParameter("max_peers_per_block", MaxPeersPerBlock)
            .GreaterThan(0)
            .Default(64);
        RegisterParameter("sweep_period", SweepPeriod)
            .Default(TDuration::Minutes(10));
    }
};

DEFINE_REFCOUNTED_TYPE(TPeerBlockTableConfig)

class TLocationConfig
    : public NYTree::TYsonSerializable
{
public:
    //! Location root path.
    Stroka Path;

    //! Maximum space chunks are allowed to occupy.
    //! (If not initialized then indicates to occupy all available space on drive).
    TNullable<i64> Quota;

    //! Minimum size the disk partition must have to make this location usable.
    TNullable<i64> MinDiskSpace;

    //! The location is considered to be full when available space becomes less than #LowWatermark.
    i64 LowWatermark;

    //! All uploads to the location are aborted when available space becomes less than #HighWatermark.
    i64 HighWatermark;

    TLocationConfig()
    {
        RegisterParameter("path", Path)
            .NonEmpty();
        RegisterParameter("quota", Quota)
            .GreaterThanOrEqual(0)
            .Default(TNullable<i64>());
        RegisterParameter("min_disk_space", MinDiskSpace)
            .GreaterThanOrEqual(0)
            .Default(TNullable<i64>());
        RegisterParameter("low_watermark", LowWatermark)
            .GreaterThanOrEqual(0)
            .Default((i64) 20 * 1024 * 1024 * 1024); // 20 Gb
        RegisterParameter("high_watermark", HighWatermark)
            .GreaterThanOrEqual(0)
            .Default((i64) 10 * 1024 * 1024 * 1024); // 10 Gb

        RegisterValidator([&] () {
            if (HighWatermark > LowWatermark) {
                THROW_ERROR_EXCEPTION("\"high_watermark\" cannot greater than \"low_watermark\"");
            }
        });
    }
};

DEFINE_REFCOUNTED_TYPE(TLocationConfig)

class TDiskHealthCheckerConfig
    : public NYTree::TYsonSerializable
{
public:
    //! Period between consequent checks.
    TDuration CheckPeriod;

    //! Size of the test file.
    i64 TestSize;

    //! Maximum time allowed for execution of a single check.
    TDuration Timeout;

    TDiskHealthCheckerConfig()
    {
        RegisterParameter("check_period", CheckPeriod)
            .Default(TDuration::Minutes(1));
        RegisterParameter("test_size", TestSize)
            .InRange(0, (i64) 1024 * 1024 * 1024)
            .Default((i64) 1024 * 1024);
        RegisterParameter("timeout", Timeout)
            .Default(TDuration::Seconds(60));
    }
};

DEFINE_REFCOUNTED_TYPE(TDiskHealthCheckerConfig)

class TMultiplexedChangelogConfig
    : public NHydra::TFileChangelogConfig
{
public:
    //! A path where multiplexed journals are stored.
    Stroka Path;

    //! Multiplexed journal record count limit.
    /*!
     *  When this limit is reached, the current multiplexed journal is rotated.
     */
    int MaxRecordCount;

    //! Multiplexed journal data size limit, in bytes.
    /*!
     *  See #MaxRecordCount.
     */
    i64 MaxDataSize;

    //! Maximum bytes of multiplexed journal to read during
    //! a single iteration of replay.
    i64 ReplayBufferSize;

    TMultiplexedChangelogConfig()
    {
        RegisterParameter("path", Path);
        RegisterParameter("max_record_count", MaxRecordCount)
            .Default(1000000)
            .GreaterThan(0);
        RegisterParameter("max_data_size", MaxDataSize)
            .Default((i64) 256 * 1024 * 1024)
            .GreaterThan(0);
        RegisterParameter("replay_buffer_size", ReplayBufferSize)
            .GreaterThan(0)
            .Default((i64) 256 * 1024 * 1024);
    }
};

DEFINE_REFCOUNTED_TYPE(TMultiplexedChangelogConfig)

//! Describes a configuration of a data node.
class TDataNodeConfig
    : public NYTree::TYsonSerializable
{
public:
    //! Period between consequent incremental heartbeats.
    TDuration IncrementalHeartbeatPeriod;

    //! Period between consequent full heartbeats.
    TNullable<TDuration> FullHeartbeatPeriod;

    //! Random delay before first heartbeat
    TDuration HeartbeatSplay;

    //! Timeout for FullHeartbeat requests.
    /*!
     *  This is usually much larger then the default RPC timeout.
     */
    TDuration FullHeartbeatTimeout;

    //! Block cache size (in bytes).
    i64 BlockCacheSize;

    //! Maximum number of cached blob chunks readers.
    int BlobReaderCacheSize;

    //! Multiplexed changelog configuration.
    TMultiplexedChangelogConfigPtr MultiplexedChangelog;

    //! Split (per chunk) changelog configuration.
    NHydra::TFileChangelogConfigPtr SplitChangelog;

    //! Maximum number of cached split changelogs.
    int MaxCachedChangelogs;

    //! Maximum number of cached opened journals.
    int ChangelogReaderCacheSize;

    //! Upload session timeout.
    /*!
     * Some activity must be happening in a session regularly (i.e. new
     * blocks uploaded or sent to other data nodes). Otherwise
     * the session expires.
     */
    TDuration SessionTimeout;

    //! Timeout for "PutBlock" requests to other data nodes.
    TDuration NodeRpcTimeout;

    //! Period between peer updates (see TPeerBlockUpdater).
    TDuration PeerUpdatePeriod;

    //! Updated expiration timeout (see TPeerBlockUpdater).
    TDuration PeerUpdateExpirationTimeout;

    //! Read requests are throttled when pending outcoming size (including bus buffers) reaches this limit.
    i64 BusOutThrottlingLimit;

    //! Write requests are throttled when pending incoming size reaches this limit.
    i64 BusInThrottlingLimit;

    //! Regular storage locations.
    std::vector<TLocationConfigPtr> StoreLocations;

    //! Cached chunks location.
    TLocationConfigPtr CacheLocation;

    //! Remote reader configuration used to download chunks into cache.
    NChunkClient::TReplicationReaderConfigPtr CacheRemoteReader;

    //! Sequential reader configuration used to download chunks into cache.
    NChunkClient::TSequentialReaderConfigPtr CacheSequentialReader;

    //! Writer configuration used to replicate chunks.
    NChunkClient::TReplicationWriterConfigPtr ReplicationWriter;

    //! Reader configuration used to repair chunks.
    NChunkClient::TReplicationReaderConfigPtr RepairReader;

    //! Writer configuration used to repair chunks.
    NChunkClient::TReplicationWriterConfigPtr RepairWriter;

    //! Controls incoming bandwidth used by replication jobs.
    NConcurrency::TThroughputThrottlerConfigPtr ReplicationInThrottler;

    //! Controls outcoming bandwidth used by replication jobs.
    NConcurrency::TThroughputThrottlerConfigPtr ReplicationOutThrottler;

    //! Controls incoming bandwidth used by repair jobs.
    NConcurrency::TThroughputThrottlerConfigPtr RepairInThrottler;

    //! Controls outcoming bandwidth used by repair jobs.
    NConcurrency::TThroughputThrottlerConfigPtr RepairOutThrottler;

    //! Keeps chunk peering information.
    TPeerBlockTableConfigPtr PeerBlockTable;

    //! Runs periodic checks against disks.
    TDiskHealthCheckerConfigPtr DiskHealthChecker;

    //! Number of writer threads per location.
    int WriteThreadCount;

    //! Maximum number of concurrent write sessions.
    int MaxWriteSessions;

    //! Maximum number of blocks to fetch via a single request per range.
    int MaxRangeReadBlockCount;

    //! Maximum number of bytes to fetch via a single request per range.
    i64 MaxRangeReadDataSize;


    TDataNodeConfig()
    {
        RegisterParameter("incremental_heartbeat_period", IncrementalHeartbeatPeriod)
            .Default(TDuration::Seconds(5));
        RegisterParameter("full_heartbeat_period", FullHeartbeatPeriod)
            .Default(Null);
        RegisterParameter("full_heartbeat_timeout", FullHeartbeatTimeout)
            .Default(TDuration::Seconds(60));
        
        RegisterParameter("block_cache_size", BlockCacheSize)
            .GreaterThan(0)
            .Default(1024 * 1024);

        RegisterParameter("blob_reader_cache_size", BlobReaderCacheSize)
            .GreaterThan(0)
            .Default(256);

        RegisterParameter("multiplexed_changelog", MultiplexedChangelog);
        RegisterParameter("split_changelog", SplitChangelog)
            .DefaultNew();
        RegisterParameter("max_cached_changelogs", MaxCachedChangelogs)
            .GreaterThan(0)
            .Default(256);
        RegisterParameter("changelog_reader_cache_size", ChangelogReaderCacheSize)
            .GreaterThan(0)
            .Default(256);

        RegisterParameter("session_timeout", SessionTimeout)
            .Default(TDuration::Seconds(120));
        RegisterParameter("node_rpc_timeout", NodeRpcTimeout)
            .Default(TDuration::Seconds(120));
        RegisterParameter("peer_update_period", PeerUpdatePeriod)
            .Default(TDuration::Seconds(30));
        RegisterParameter("peer_update_expiration_timeout", PeerUpdateExpirationTimeout)
            .Default(TDuration::Seconds(40));

        RegisterParameter("bus_out_throttling_limit", BusOutThrottlingLimit)
            .GreaterThan(0)
            .Default((i64) 512 * 1024 * 1024);
        RegisterParameter("bus_in_throttling_limit", BusInThrottlingLimit)
            .GreaterThan(0)
            // TODO(babenko): provide some meaningful default
            .Default((i64) 100 * 1024 * 1024 * 1024);

        RegisterParameter("store_locations", StoreLocations)
            .NonEmpty();
        RegisterParameter("cache_location", CacheLocation)
            .DefaultNew();

        RegisterParameter("cache_remote_reader", CacheRemoteReader)
            .DefaultNew();
        RegisterParameter("cache_sequential_reader", CacheSequentialReader)
            .DefaultNew();
        RegisterParameter("replication_writer", ReplicationWriter)
            .DefaultNew();
        RegisterParameter("repair_reader", RepairReader)
            .DefaultNew();
        RegisterParameter("repair_writer", RepairWriter)
            .DefaultNew();

        RegisterParameter("replication_in_throttler", ReplicationInThrottler)
            .DefaultNew();
        RegisterParameter("replication_out_throttler", ReplicationOutThrottler)
            .DefaultNew();
        RegisterParameter("repair_in_throttler", RepairInThrottler)
            .DefaultNew();
        RegisterParameter("repair_out_throttler", RepairOutThrottler)
            .DefaultNew();

        RegisterParameter("peer_block_table", PeerBlockTable)
            .DefaultNew();

        RegisterParameter("disk_health_checker", DiskHealthChecker)
            .DefaultNew();

        RegisterParameter("write_thread_count", WriteThreadCount)
            .Default(1)
            .GreaterThanOrEqual(1);
            
        RegisterParameter("max_write_sessions", MaxWriteSessions)
            .Default(1000)
            .GreaterThanOrEqual(1);

        RegisterParameter("max_range_read_block_count", MaxRangeReadBlockCount)
            .GreaterThan(0)
            .Default(100000);
        RegisterParameter("max_range_read_data_size", MaxRangeReadDataSize)
            .GreaterThan(0)
            .Default((i64) 256 * 1024 * 1024);

        RegisterInitializer([&] () {
            // Expect many splits -- adjust configuration.
            SplitChangelog->FlushBufferSize = (i64) 16 * 1024 * 1024;
            SplitChangelog->FlushPeriod = TDuration::Seconds(15);
        });
    }
};

DEFINE_REFCOUNTED_TYPE(TDataNodeConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT
