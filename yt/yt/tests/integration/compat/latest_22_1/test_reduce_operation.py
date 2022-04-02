from original_tests.yt.yt.tests.integration.tests.controller.test_reduce_operation \
    import TestSchedulerReduceCommands as BaseTestReduceCommands


class TestReduceCommandsCompatUpToCA(BaseTestReduceCommands):
    ARTIFACT_COMPONENTS = {
        "22_1": ["master", "node", "job-proxy", "exec", "tools"],
        "trunk": ["scheduler", "controller-agent", "proxy", "http-proxy"],
    }

    DELTA_CONTROLLER_AGENT_CONFIG = {
        "controller_agent": {
            "enable_table_column_renaming": False,
        },
    }
