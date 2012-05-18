#pragma once

#include "public.h"

#include <ytlib/misc/hash.h>
#include <ytlib/misc/ref.h>
#include <ytlib/actions/action_queue.h>
#include <ytlib/actions/future.h>

#include <util/system/file.h>

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

//! Asynchronous wrapper around TChangeLog.
/*!
 * This class implements (more-or-less) non-blocking semantics for working with
 * the changelog. Blocking can occur eventually when the internal buffers
 * overflow.
 *
 * \see #UnflushedBytesThreshold
 * \see #UnflushedRecordsThreshold
 */
class TAsyncChangeLog
    : private TNonCopyable
{
public:
    TAsyncChangeLog(const TChangeLogPtr& changeLog);
    ~TAsyncChangeLog();

    typedef TFuture<void> TAppendResult;
    typedef TPromise<void> TAppendPromise;

    //! Enqueues record to be appended to the changelog.
    /*!
     * Internally, asynchronous append to the changelog goes as follows.
     * Firstly, the record is marked as 'Unflushed' and enqueued to the flush queue.
     * Secondly, as soon as the queue becomes synchronized with the disk state
     * the promise is fulfilled. At this moment caller can determine whether
     * the record was written to the disk.
     *
     * Note that promise is not fulfilled when an error occurs.
     * In this case the promise is never fulfilled.
     *
     * \param recordId Consecutive record id.
     * \param data Actual record content.
     * \returns Promise to fulfill when the record will be flushed.
     *
     * \see TChangeLog::Append
     */
    TAppendResult Append(i32 recordId, const TSharedRef& data);

    //! Flushes the changelog.
    //! \see TChangeLog::Flush
    void Flush();

    //! Reads records from the changelog.
    //! \see TChangeLog::Read
    //! Can return less records than recordCount
    void Read(i32 firstRecordId, i32 recordCount, yvector<TSharedRef>* result);

    //! Truncates the changelog at the specified record.
    //! \see TChangeLog::Truncate
    void Truncate(i32 atRecordId);

    //! Finalizes the changelog.
    //! \see TChangeLog::Finalize
    void Finalize();
        
    i32 GetId() const;
    i32 GetPrevRecordCount() const;
    i32 GetRecordCount() const;
    bool IsFinalized() const;

    // XXX(sandello): This is very-very-very dirty. Dirty-dirty-dirty. 
    // Bad, bad, nasty --girl-- static initialization fiasco.
    static void Shutdown();

private:
    class TChangeLogQueue;
    typedef TIntrusivePtr<TChangeLogQueue> TChangeLogQueuePtr;

    class TImpl;

    TChangeLogPtr ChangeLog;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
