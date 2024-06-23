# flake8: noqa
# I'd like to disable only E124 and E128 but flake cannot ignore specific
# warnings for the entire file at the moment.
# [E124] closing bracket does not match visual indentation
# [E128] continuation line under-indented for visual indent

from .common.sensors import *

from yt_dashboard_generator.dashboard import Dashboard, Rowset
from yt_dashboard_generator.specific_tags.tags import TemplateTag
from yt_dashboard_generator.backends.monitoring.sensors import MonitoringExpr
from yt_dashboard_generator.backends.monitoring import MonitoringTextDashboardParameter


def build_versions():
    return (Rowset()
        .stack(True)
        .aggr("host")
        .row()
            .cell("Controller versions", FlowController("yt.build.version"))
            .cell("Worker versions", FlowWorker("yt.build.version"))
    ).owner

def build_resource_usage():
    return (Rowset()
        .stack(False)
        .all("host")
        .row()
            .cell("Total CPU (Controller)", FlowController("yt.resource_tracker.total_cpu")
                .aggr("thread")
                .unit("UNIT_PERCENT"))
            .cell(
                "Busiest thread (Controller)",
                MonitoringExpr(FlowController("yt.resource_tracker.utilization"))
                    .unit("UNIT_PERCENT_UNIT")
                    .all("thread")
                    .group_by_labels("host", "v -> group_lines(\"sum\", top_avg(1, v))"))
            .cell("Memory (Controller)", FlowController("yt.resource_tracker.memory_usage.rss").unit("UNIT_BYTES_SI"))
        .row()
            .cell("Total CPU (Worker)", FlowWorker("yt.resource_tracker.total_cpu")
                .aggr("thread")
                .unit("UNIT_PERCENT"))
            .cell(
                "Busiest thread (Worker)",
                MonitoringExpr(FlowWorker("yt.resource_tracker.utilization"))
                    .unit("UNIT_PERCENT_UNIT")
                    .all("thread")
                    .group_by_labels("host", "v -> group_lines(\"sum\", top_avg(1, v))"))
            .cell("Memory (Worker)", FlowWorker("yt.resource_tracker.memory_usage.rss").unit("UNIT_BYTES_SI"))
    ).owner

def build_flow_layout():
    return (Rowset()
        .stack(False)
        .row()
            .cell("Registered workers count", FlowController("yt.flow.controller.worker_count").unit("UNIT_COUNT"))
            .cell("Computations count", FlowController("yt.flow.controller.computation_count").unit("UNIT_COUNT"))
            .cell("Partitions count", FlowController("yt.flow.controller.partition_count").unit("UNIT_COUNT"))
    ).owner

def build_flow_layout_mutations():
    return (Rowset()
        .stack(False)
        .row()
            .cell("Job statuses", FlowController("yt.flow.controller.job_status.*").unit("UNIT_COUNT"))
            .cell("Layout mutations", FlowController("yt.flow.controller.mutations.*.rate").unit("UNIT_COUNTS_PER_SECOND"))
            .cell("Job manage mutations", FlowController("yt.flow.controller.job_manager.*.rate").unit("UNIT_COUNTS_PER_SECOND"))
    ).owner

def build_watermarks():
    return (Rowset()
        .stack(False)
        .row()
            .cell(
                "System watermark",
                FlowController("yt.flow.controller.watermark_lag.system_timestamp")
                    .unit("UNIT_SECONDS"))
            .cell(
                "User watermarks",
                MonitoringExpr(FlowController("yt.flow.controller.watermark_lag.user_timestamp"))
                    .all("user_timestamp_id")
                    .alias("{{user_timestamp_id}}")
                    .unit("UNIT_SECONDS"))
    ).owner

def build_lags():
    return (Rowset()
        .stack(False)
        .all("computation_id")
        .all("stream_id")
        .row()
            .cell(
                "Stream lags",
                MonitoringExpr(FlowController("yt.flow.controller.computation.*streams.lag"))
                    .alias("{{computation_id}}/{{stream_id}}")
                    .unit("UNIT_COUNT"))
            .cell(
                "Stream time lags",
                MonitoringExpr(FlowController("yt.flow.controller.computation.*streams.time_lag"))
                    .alias("{{computation_id}}/{{stream_id}}")
                    .unit("UNIT_SECONDS"))
            .cell(
                "Stream size lags",
                MonitoringExpr(FlowController("yt.flow.controller.computation.*streams.byte_size_lag"))
                    .alias("{{computation_id}}/{{stream_id}}")
                    .unit("UNIT_BYTES_SI"))
    )

def build_streams():
    return (Rowset()
        .stack(True)
        .aggr("host")
        .all("computation_id")
        .all("stream_id")
        .row()
            .cell(
                "Input messages rate",
                MonitoringExpr(FlowWorker("yt.flow.worker.computation.input_messages_count.rate"))
                    .alias("{{computation_id}}/{{stream_id}}")
                    .unit("UNIT_COUNTS_PER_SECOND"))
            .cell(
                "Input messages bytes rate",
                MonitoringExpr(FlowWorker("yt.flow.worker.computation.input_messages_size.rate"))
                    .alias("{{computation_id}}/{{stream_id}}")
                    .unit("UNIT_BYTES_SI_PER_SECOND"))
            .cell(
                "Output messages rate",
                MonitoringExpr(FlowWorker("yt.flow.worker.computation.output_messages_count.rate"))
                    .alias("{{computation_id}}/{{stream_id}}")
                    .unit("UNIT_COUNTS_PER_SECOND"))
            .cell(
                "Output messages bytes rate",
                MonitoringExpr(FlowWorker("yt.flow.worker.computation.output_messages_size.rate"))
                    .alias("{{computation_id}}/{{stream_id}}")
                    .unit("UNIT_BYTES_SI_PER_SECOND"))
    )

def build_buffers():
    return (Rowset()
        .stack(True)
        .aggr("host")
        .all("computation_id")
        .row()
            .cell(
                "Input buffers size",
                MonitoringExpr(FlowWorker("yt.flow.worker.buffer_state.computations.input_buffer_message_size"))
                    .alias("{{computation_id}}")
                    .unit("UNIT_BYTES_SI"))
            .cell(
                "Output buffers size",
                MonitoringExpr(FlowWorker("yt.flow.worker.buffer_state.computations.output_buffer_message_size"))
                    .alias("{{computation_id}}")
                    .unit("UNIT_BYTES_SI"))
    )

def build_partition_store_commits():
    return (Rowset()
        .stack(False)
        .all("computation_id")
        .row()
            .cell(
                "Partition store commit rate",
                MonitoringExpr(FlowWorker("yt.flow.worker.computation.partition_store.commit_total.rate"))
                    .aggr("host")
                    .unit("UNIT_REQUESTS_PER_SECOND"))
            .cell(
                "Partition store failed commit rate",
                MonitoringExpr(FlowWorker("yt.flow.worker.computation.partition_store.commit_failed.rate"))
                    .aggr("host")
                    .unit("UNIT_REQUESTS_PER_SECOND"))
            .cell(
                "Partition store commit time",
                MonitoringExpr(FlowWorker("yt.flow.worker.computation.partition_store.commit_time.max"))
                    .all("host")
                    .top()
                    .unit("UNIT_SECONDS"))
            .cell(
                "Input messages lookup time",
                MonitoringExpr(FlowWorker("yt.flow.worker.computation.partition_store.input_messages.lookup_time.max"))
                    .all("host")
                    .top()
                    .unit("UNIT_SECONDS"))
    )

def build_pipeline():
    d = Dashboard()
    d.add(build_versions())
    d.add(build_resource_usage())
    d.add(build_flow_layout())
    d.add(build_flow_layout_mutations())
    d.add(build_watermarks())
    d.add(build_lags())
    d.add(build_streams())
    d.add(build_buffers())
    d.add(build_partition_store_commits())

    d.set_title("[YT Flow] Pipeline general")
    d.add_parameter("project", "Pipeline project", MonitoringTextDashboardParameter())
    d.add_parameter("cluster", "Cluster", MonitoringTextDashboardParameter())

    return (d
        .value("project", TemplateTag("project"))
        .value("cluster", TemplateTag("cluster")))
