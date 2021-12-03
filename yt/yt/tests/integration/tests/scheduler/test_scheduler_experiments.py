import builtins

from yt_env_setup import YTEnvSetup, is_asan_build, Restarter, CONTROLLER_AGENTS_SERVICE
from yt_commands import (
    authors, clean_operations, print_debug, wait, wait_breakpoint, release_breakpoint, with_breakpoint, create,
    get, exists, remove, abort_job, ls, set,
    reduce, map_reduce, sort, erase, remote_copy, get_driver, vanilla,
    write_table, map, merge, get_operation, list_operations, sync_create_cells, raises_yt_error)

import yt.environment.init_operation_archive as init_operation_archive

from flaky import flaky
import math
import time

##################################################################


class TestSchedulerExperiments(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_SCHEDULERS = 1
    NUM_CONTROLLER_AGENTS = 2

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "experiments": {
                "exp_a1": {
                    "fraction": 0.6,
                    "ticket": "YTEXP-1",
                    "ab_treatment_group": {
                        "fraction": 0.5,
                        "scheduler_spec_template_patch": {
                            "foo_spec_template": "exp_a1.treatment",
                        },
                        "scheduler_spec_patch": {
                            "foo_spec": "exp_a1.treatment",
                        },
                    },
                },
                "exp_a2": {
                    "fraction": 0.3,
                    "ticket": "YTEXP-2",
                    "groups": {
                        "group1": {
                            "fraction": 1.0 / 3,
                            "scheduler_spec_patch": {
                                "foo_spec": "exp_a2.group1",
                            },
                        },
                        "group2": {
                            "fraction": 2.0 / 3,
                            "scheduler_spec_patch": {
                                "foo_spec": "exp_a2.group1",
                            },
                        },
                    },
                },
                "exp_b1": {
                    "fraction": 0.5,
                    "ticket": "YTEXP-3",
                    "dimension": "other_dimension",
                    "ab_treatment_group": {
                        "fraction": 1.0,
                        "controller_agent_tag": "tagged",
                        "scheduler_spec_patch": {
                            "foo_spec": "exp_b1.treatment",
                        },
                    },
                },
                "exp_b2": {
                    "fraction": 0.3,
                    "ticket": "YTEXP-4",
                    "dimension": "other_dimension",
                    "filter": "[/type] = 'map'",
                    "ab_treatment_group": {
                        "fraction": 1.0,
                        "scheduler_spec_patch": {
                            "foo_spec": "exp_b2.treatment",
                        },
                    },
                },
            },
        },
    }

    controller_agent_tag_to_address = dict()
    controller_agent_counter = 0

    @classmethod
    def modify_controller_agent_config(cls, config):
        if cls.controller_agent_counter == 2:
            cls.controller_agent_counter = 0

        controller_agent_tags = ["tagged", "default"]

        tag = controller_agent_tags[cls.controller_agent_counter]
        config["controller_agent"]["tags"] = [tag]
        cls.controller_agent_tag_to_address[tag] = "localhost:" + str(config["rpc_port"])

        cls.controller_agent_counter += 1

    @classmethod
    def setup_class(cls, **kwargs):
        super(TestSchedulerExperiments, cls).setup_class(**kwargs)

    @authors("max42")
    def test_get_operation(self):
        create("table", "//tmp/t_in")
        write_table("//tmp/t_in", [{"a": 1}])

        op = map(in_=["//tmp/t_in"],
                 out=[],
                 command=with_breakpoint("BREAKPOINT"),
                 track=False,
                 spec={"experiment_overrides": ["exp_a1.treatment"]})
        wait_breakpoint()
        info = get_operation(op.id, attributes=["experiment_assignments", "experiment_assignment_names", "spec"])
        assert info["experiment_assignment_names"] == ["exp_a1.treatment"]
        assert info["experiment_assignments"] == [{
            "experiment": "exp_a1",
            "group": "treatment",
            "experiment_uniform_sample": 0.0,
            "group_uniform_sample": 0.0,
            "ticket": "YTEXP-1",
            "dimension": "default",
            "effect": {
                "fraction": 0.5,
                "scheduler_spec_template_patch": {
                    "foo_spec_template": "exp_a1.treatment",
                },
                "scheduler_spec_patch": {
                    "foo_spec": "exp_a1.treatment",
                },
            }
        }]
        assert info["spec"]["foo_spec_template"] == "exp_a1.treatment"
        assert info["spec"]["foo_spec"] == "exp_a1.treatment"

    @authors("max42")
    def test_scheduler_spec_patches(self):
        create("table", "//tmp/t_in")

        op = map(in_=["//tmp/t_in"],
                 out=[],
                 command="exit 0",
                 spec={"experiment_overrides": ["exp_a1"]})

        info = get_operation(op.id, attributes=["spec"])
        assert info["spec"]["foo_spec"] == "exp_a1.treatment"
        assert info["spec"]["foo_spec_template"] == "exp_a1.treatment"

        op = map(
            in_=["//tmp/t_in"],
            out=[],
            command="exit 0",
            spec={
                "experiment_overrides": ["exp_a1"],
                "foo_spec": "custom",
                "foo_spec_template": "custom",
            })

        info = get_operation(op.id, attributes=["spec"])
        assert info["spec"]["foo_spec"] == "exp_a1.treatment"
        assert info["spec"]["foo_spec_template"] == "custom"

    @authors("max42")
    def test_controller_agent_tag(self):
        def get_controller_agent_address(events):
            addresses = [event["attributes"].get("controller_agent_address") for event in events]
            addresses = [address for address in addresses if address is not None]
            addresses = list(builtins.set(addresses))
            assert len(addresses) == 1
            return addresses[0]

        create("table", "//tmp/t_in")

        op = map(in_=["//tmp/t_in"],
                 out=[],
                 command="exit 0",
                 spec={"experiment_overrides": ["exp_b1.control"]})

        assert get_controller_agent_address(get_operation(op.id, attributes=["events"])["events"]) == \
               TestSchedulerExperiments.controller_agent_tag_to_address["default"]

        op = map(in_=["//tmp/t_in"],
                 out=[],
                 command="exit 0",
                 spec={"experiment_overrides": ["exp_b1.treatment"]})

        assert get_controller_agent_address(get_operation(op.id, attributes=["events"])["events"]) == \
               TestSchedulerExperiments.controller_agent_tag_to_address["tagged"]

    @authors("max42")
    def test_wrong_experiment_override(self):
        create("table", "//tmp/t_in")

        with raises_yt_error('Experiment "nonexistent" is not known'):
            map(in_=["//tmp/t_in"],
                out=[],
                command="exit 0",
                spec={"experiment_overrides": ["nonexistent"]})

        with raises_yt_error('Group "nonexistent" is not known'):
            map(in_=["//tmp/t_in"],
                out=[],
                command="exit 0",
                spec={"experiment_overrides": ["exp_a1.nonexistent"]})

    @authors("max42")
    @flaky(max_runs=3)
    def test_distribution(self):
        create("table", "//tmp/t_in")
        # In order to distinguish current test run from other runs (note flaky decorator above)
        # we mark all operations from this test with a unique random guid taken from created table id.
        guid = get("//tmp/t_in/@id")
        operation_count = 200
        for i in range(operation_count):
            map(in_=["//tmp/t_in"],
                out=[],
                command="exit 0",
                spec={"annotations": {"tag": "distribution" + guid}},
                track=False)

        wait(lambda: get("//tmp/t_in/@lock_count") == 0)

        operations = list_operations(
            limit=1000,
            filter="distribution" + guid,
            attributes=["experiment_assignment_names"],
            include_archive=False,
        )["operations"]

        assert len(operations) == 200

        def extract_assignments(operation):
            assignment_names = operation["experiment_assignment_names"]
            assert len(assignment_names) <= 2
            dimension_to_name = {"default": None, "other_dimension": None}
            for assignment_name in assignment_names:
                if "exp_a" in assignment_name:
                    dimension = "default"
                elif "exp_b" in assignment_name:
                    dimension = "other_dimension"
                else:
                    assert False
                assert dimension_to_name[dimension] is None
                dimension_to_name[dimension] = assignment_name
            return dimension_to_name["default"], dimension_to_name["other_dimension"]

        assignments = [extract_assignments(operation) for operation in operations]

        def validate_count(default, other_dimension, expected_fraction):
            expected_count = expected_fraction * len(operations)
            # We use three sigma rule (cf. Wikipedia article) since we have a binomial distribution
            # which is pretty close to normal distribution under our number of experiments.
            # Single test is about to succeed with probability 99.7%, we conduct ~10 tests
            # and, finally, mark test as flaky setting up at most three runs. This should be robust
            # enough not to ever see this test broken in CI :)
            sigma = math.sqrt(len(operations) * expected_fraction * (1 - expected_fraction))
            lower_bound = math.floor(expected_count - 3 * sigma)
            upper_bound = math.floor(expected_count + 3 * sigma)
            print_debug("Validating count for ({}, {}) to be between {} and {} "
                        "(expected fraction = {:.3f}, expected value = {:.3f}, sigma = {:.3f})".format(
                            default, other_dimension, lower_bound, upper_bound,
                            expected_fraction, expected_count, sigma))

            def matches(assignment, expected):
                return expected == "*" or (assignment is not None and assignment.startswith(expected))

            actual_count = len([assignment for assignment in assignments if
                                matches(assignment[0], default) and matches(assignment[1], other_dimension)])
            print_debug("Actual count = {}".format(actual_count))

            assert lower_bound <= actual_count <= upper_bound

        validate_count("*", "*", 1.0)
        validate_count("*", "exp_b1", 0.5)
        validate_count("*", "exp_b1.treatment", 0.5)
        validate_count("*", "exp_b1.control", 0.0)
        validate_count("*", "exp_b2", 0.3)
        validate_count("*", "exp_b2.treatment", 0.3)
        validate_count("*", "exp_b2.control", 0.0)
        validate_count("exp_a1", "*", 0.6)
        validate_count("exp_a1.control", "*", 0.3)
        validate_count("exp_a1.treatment", "*", 0.3)
        validate_count("exp_a2", "*", 0.3)
        validate_count("exp_a2.group1", "*", 0.1)
        validate_count("exp_a2.group2", "*", 0.2)
        validate_count("exp_a1", "exp_b1", 0.3)
        validate_count("exp_a2.group1", "exp_b1", 0.05)

    @authors("max42")
    def test_filters(self):
        create("table", "//tmp/t_in")
        create("table", "//tmp/t_out")

        with raises_yt_error('Operation does not match filter of experiment "exp_b2"'):
            merge(in_=["//tmp/t_in"],
                  out="//tmp/t_out",
                  spec={"experiment_overrides": ["exp_b2.treatment"]})


class TestSchedulerExperimentsArchivation(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_SCHEDULERS = 1
    NUM_CONTROLLER_AGENTS = 2

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "experiments": {
                "exp": {
                    "fraction": 0.5,
                    "ticket": "YTEXP-1",
                    "ab_treatment_group": {
                        "fraction": 0.5,
                        "scheduler_spec_patch": {
                            "foo_spec": "exp_a1.treatment",
                        },
                    },
                },
            },
            "operations_cleaner": {
                "enable": True,
                # Analyze all operations each 100ms
                "analysis_period": 100,
                # Cleanup all operations
                "hard_retained_operation_count": 0,
                "clean_delay": 0,
                "remove_batch_timeout": 100,
                "archive_batch_timeout": 100,
                "max_removal_sleep_delay": 100,
            },
        },
    }

    @authors("max42")
    def test_archivation(self):
        sync_create_cells(1)
        init_operation_archive.create_tables_latest_version(
            self.Env.create_native_client(), override_tablet_cell_bundle="default"
        )

        create("table", "//tmp/t_in")
        write_table("//tmp/t_in", [{"a": 1}])

        op = map(in_=["//tmp/t_in"],
                 out=[],
                 command=with_breakpoint("BREAKPOINT"),
                 track=False,
                 spec={"experiment_overrides": ["exp"]})

        def operation_present_in_cypress():
            return exists(op.get_path())

        wait(lambda: operation_present_in_cypress())
        wait_breakpoint()

        cypress_info = get_operation(
            op.id,
            attributes=["experiment_assignments", "experiment_assignment_names", "spec"])
        release_breakpoint()

        wait(lambda: not operation_present_in_cypress())

        archive_info = get_operation(
            op.id,
            attributes=["experiment_assignments", "experiment_assignment_names", "spec"])

        assert cypress_info == archive_info


class TestUserJobAndJobIOExperiments(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_SCHEDULERS = 1
    NUM_CONTROLLER_AGENTS = 1

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "experiments": {
                "exp_a1": {
                    "fraction": 0.4,
                    "ticket": "ytexp-1",
                    "ab_treatment_group": {
                        "fraction": 0.5,
                        "controller_user_job_spec_template_patch": {
                            "foo_spec_template": "patched",
                        },
                        "controller_user_job_spec_patch": {
                            "foo_spec": "patched",
                        },
                        "controller_job_io_template_patch": {
                            "bar_spec_template": "patched",
                        },
                        "controller_job_io_patch": {
                            "bar_spec": "patched",
                        }
                    },
                },
                "exp_b1": {
                    "fraction": 0.4,
                    "ticket": "ytexp-2",
                    "ab_treatment_group": {
                        "fraction": 0.5,
                        "controller_user_job_spec_template_patch": {
                            "bar_spec": "patched",
                        },
                        "controller_user_job_spec_patch": {
                            "foo_spec": "patched",
                        },
                        "controller_job_io_template_patch": {
                            "bar_spec": "patched",
                        },
                        "controller_job_io_patch": {
                            "foo_spec": "patched",
                        }
                    },
                },
                "exp_c1": {
                    "fraction": 0.1,
                    "ticket": "ytexp-3",
                    "ab_treatment_group": {
                        "fraction": 0.5,
                        "controller_job_io_patch": {
                            "foo_spec": "patched",
                        }
                    },
                },
            },
        },
    }

    NUM_REMOTE_CLUSTERS = 1

    NUM_MASTERS_REMOTE_0 = 1
    NUM_SCHEDULERS_REMOTE_0 = 0

    REMOTE_CLUSTER_NAME = "remote_0"

    @classmethod
    def setup_class(cls):
        super(TestUserJobAndJobIOExperiments, cls).setup_class()
        cls.remote_driver = get_driver(cluster=cls.REMOTE_CLUSTER_NAME)

    @authors("alexkolodezny")
    def test_user_job_and_job_io_patches(self):
        create("table", "//tmp/t_in")
        write_table("//tmp/t_in", [{"key": 1}])
        create("table", "//tmp/t_out")

        def check_user_job(user_job):
            assert user_job["foo_spec_template"] == "patched"
            assert user_job["foo_spec"] == "patched"

        def check_job_io(job_io):
            assert job_io["bar_spec_template"] == "patched"
            assert job_io["bar_spec"] == "patched"

        op = map(in_=["//tmp/t_in"],
                 out=["//tmp/t_out"],
                 command="cat",
                 spec={"experiment_overrides": ["exp_a1.treatment"]})
        spec = get_operation(op.id, attributes=["full_spec"])["full_spec"]
        check_user_job(spec["mapper"])
        check_job_io(spec["job_io"])
        check_job_io(spec["auto_merge"]["job_io"])

        op = sort(in_="//tmp/t_in",
                  out="//tmp/t_in",
                  sort_by="key",
                  spec={"experiment_overrides": ["exp_a1.treatment"]})
        spec = get_operation(op.id, attributes=["full_spec"])["full_spec"]
        check_job_io(spec["partition_job_io"])
        check_job_io(spec["sort_job_io"])
        check_job_io(spec["merge_job_io"])
        check_job_io(spec["auto_merge"]["job_io"])

        op = reduce(in_=["//tmp/t_in"],
                    out=["//tmp/t_out"],
                    command="cat",
                    reduce_by="key",
                    spec={"experiment_overrides": ["exp_a1.treatment"]})
        spec = get_operation(op.id, attributes=["full_spec"])["full_spec"]
        check_user_job(spec["reducer"])
        check_job_io(spec["job_io"])
        check_job_io(spec["auto_merge"]["job_io"])

        op = map_reduce(
            in_="//tmp/t_in",
            out="//tmp/t_out",
            sort_by="key",
            reduce_by="key",
            mapper_command="cat",
            reducer_command="cat",
            reduce_combiner_command="cat",
            spec={"experiment_overrides": ["exp_a1.treatment"]})

        spec = get_operation(op.id, attributes=["full_spec"])["full_spec"]
        check_user_job(spec["reducer"])
        check_user_job(spec["mapper"])
        check_user_job(spec["reduce_combiner"])
        check_job_io(spec["sort_job_io"])
        check_job_io(spec["map_job_io"])
        check_job_io(spec["reduce_job_io"])
        check_job_io(spec["auto_merge"]["job_io"])

        op = map_reduce(
            in_="//tmp/t_in",
            out="//tmp/t_out",
            sort_by="key",
            reduce_by="key",
            reducer_command="cat",
            spec={"experiment_overrides": ["exp_a1.treatment"]})

        spec = get_operation(op.id, attributes=["full_spec"])["full_spec"]
        assert "mapper" not in spec
        assert "reduce_combiner" not in spec
        check_user_job(spec["reducer"])
        check_job_io(spec["sort_job_io"])
        check_job_io(spec["map_job_io"])
        check_job_io(spec["reduce_job_io"])
        check_job_io(spec["auto_merge"]["job_io"])

        op = merge(in_="//tmp/t_in",
                   out="//tmp/t_out",
                   spec={"experiment_overrides": ["exp_a1.treatment"]})
        spec = get_operation(op.id, attributes=["full_spec"])["full_spec"]
        check_job_io(spec["job_io"])
        check_job_io(spec["auto_merge"]["job_io"])

        create("table", "//tmp/t_in", driver=self.remote_driver)

        op = remote_copy(in_="//tmp/t_in",
                         out="//tmp/t_out",
                         spec={"cluster_name": self.REMOTE_CLUSTER_NAME,
                               "experiment_overrides": ["exp_a1.treatment"]})
        spec = get_operation(op.id, attributes=["full_spec"])["full_spec"]
        check_job_io(spec["job_io"])
        check_job_io(spec["auto_merge"]["job_io"])

        op = erase("//tmp/t_in",
                   spec={"experiment_overrides": ["exp_a1.treatment"]})
        spec = get_operation(op.id, attributes=["full_spec"])["full_spec"]
        check_job_io(spec["job_io"])
        check_job_io(spec["auto_merge"]["job_io"])

        op = vanilla(
            spec={
                "experiment_overrides": ["exp_a1.treatment"],
                "tasks": {
                    "task_a": {
                        "job_count": 1,
                        "command": "true",
                    },
                    "task_b": {
                        "job_count": 1,
                        "command": "true",
                    },
                }})

        spec = get_operation(op.id, attributes=["full_spec"])["full_spec"]
        check_job_io(spec["auto_merge"]["job_io"])
        check_user_job(spec["tasks"]["task_a"])
        check_user_job(spec["tasks"]["task_b"])
        check_job_io(spec["tasks"]["task_a"]["job_io"])
        check_job_io(spec["tasks"]["task_b"]["job_io"])

    @authors("alexkolodezny")
    def test_patches_and_template_patches(self):
        create("table", "//tmp/t_in")
        write_table("//tmp/t_in", [{"key": 1}])

        op = map(
            in_=["//tmp/t_in"],
            out=[],
            command="exit 0",
            spec={
                "experiment_overrides": ["exp_b1.treatment"],
                "mapper": {
                    "foo_spec": "original",
                    "bar_spec": "original",
                },
                "job_io": {
                    "foo_spec": "original",
                    "bar_spec": "original",
                }})
        spec = get_operation(op.id, attributes=["full_spec"])["full_spec"]
        assert spec["mapper"]["foo_spec"] == "patched"
        assert spec["mapper"]["bar_spec"] == "original"
        assert spec["job_io"]["foo_spec"] == "patched"
        assert spec["job_io"]["bar_spec"] == "original"

    @authors("max42")
    def test_job_io_patch_for_bare_sort(self):
        create("table", "//tmp/t")
        write_table("//tmp/t", [{"key": 1}])

        op = sort(
            in_=["//tmp/t"],
            out="//tmp/t",
            sort_by=["key"],
            spec={
                "experiment_overrides": ["exp_c1.treatment"]
            })
        spec = get_operation(op.id, attributes=["full_spec"])["full_spec"]
        assert spec["sort_job_io"]["foo_spec"] == "patched"
        assert spec["partition_job_io"]["foo_spec"] == "patched"
        assert spec["merge_job_io"]["foo_spec"] == "patched"


class TestControllerFeatures(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_SCHEDULERS = 1
    NUM_CONTROLLER_AGENTS = 1
    USE_DYNAMIC_TABLES = True

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "watchers_update_period": 100,
            "operations_update_period": 10,
            "operations_cleaner": {
                "enable": False,
                "analysis_period": 100,
                # Cleanup all operations
                "hard_retained_operation_count": 0,
                "clean_delay": 0,
            },
            "enable_job_reporter": True,
            "enable_job_spec_reporter": True,
            "enable_job_stderr_reporter": True,
        },
    }

    def setup(self):
        sync_create_cells(1)
        init_operation_archive.create_tables_latest_version(
            self.Env.create_native_client(), override_tablet_cell_bundle="default"
        )

    def teardown(self):
        remove("//sys/operations_archive", force=True)

    @authors("alexkolodezny")
    def test_controller_features(self):
        create("table", "//tmp/t_in")
        write_table("//tmp/t_in", [{"key": 1}])

        op = map(
            in_=["//tmp/t_in"],
            out=[],
            command="sleep 1",
        )

        def check_features(op):
            features = get_operation(op.id, attributes=["controller_features"])["controller_features"]
            assert len(features) == 2
            operation = features[0]
            task = features[1]
            if "task_name" in operation["tags"]:
                task, operation = operation, task
            assert task["tags"]["task_name"] == "map"
            assert task["features"]["wall_time"] > 1000
            assert operation["features"]["wall_time"] > 1000
            if not is_asan_build():
                assert operation["features"]["peak_controller_memory_usage"] > 0

        check_features(op)

        op = map(
            in_=["//tmp/t_in"],
            out=[],
            command="sleep 1 && exit 1",
            spec={
                "max_failed_job_count": 0,
            },
            track=False,
        )

        with raises_yt_error("Process exited with code 1"):
            op.track()

        check_features(op)

        op = map(
            in_=["//tmp/t_in"],
            out=[],
            command="sleep 1",
        )

        clean_operations()

        check_features(op)

        op = map(
            in_=["//tmp/t_in"],
            out=[],
            command="sleep 1 && exit 1",
            spec={
                "max_failed_job_count": 0,
            },
            track=False,
        )

        with raises_yt_error("Process exited with code 1"):
            op.track()

        clean_operations()

        check_features(op)

    @authors("alexkolodezny")
    def test_various_controller_features(self):
        create("table", "//tmp/t_in")
        write_table("//tmp/t_in", [{"key": 1}])

        op = map(
            in_=["//tmp/t_in"],
            out=[],
            command="sleep 1",
            spec={
                "job_count": 1,
            },
        )

        def get_features(op):
            features = get(op.get_path() + "/@controller_features")
            operation_features = features[0]
            task_features = features[1]
            if "task_name" in operation_features["tags"]:
                task_features, operation_features = operation_features, task_features
            return operation_features, task_features

        operation_features, task_features = get_features(op)
        assert operation_features["features"]["operation_count"] == 1.0
        assert operation_features["features"]["job_count.completed.non-interrupted"] == 1.0
        assert operation_features["tags"]["operation_type"] == "map"
        assert operation_features["tags"]["total_job_count"] == 1
        assert "total_estimated_input_data_weight" in operation_features["tags"]
        assert "total_estimated_input_row_count" in operation_features["tags"]
        assert "authenticated_user" in operation_features["tags"]
        assert "authenticated_user" in task_features["tags"]
        assert "total_input_data_weight" in task_features["tags"]
        assert "total_input_row_count" in task_features["tags"]
        assert task_features["tags"]["total_job_count"] == 1
        assert task_features["tags"]["task_name"] == "map"

        op = map(
            in_=["//tmp/t_in"],
            out=[],
            command="sleep 1 && exit 1",
            spec={
                "max_failed_job_count": 0,
            },
            track=False,
        )

        with raises_yt_error("Process exited with code 1"):
            op.track()

        operation_features = get_features(op)[0]
        assert operation_features["features"]["operation_count"] == 1.0
        assert operation_features["features"]["job_count.failed"] == 1.0
        assert operation_features["tags"]["total_job_count"] == 1

        op = map(
            in_=["//tmp/t_in"],
            out=[],
            command=with_breakpoint("BREAKPOINT"),
            track=False,
            spec={
                "max_failed_job_count": 0,
            },
        )

        wait_breakpoint()

        for job in op.get_running_jobs():
            abort_job(job)

        release_breakpoint()

        op.track()

        operation_features = get_features(op)[0]
        assert operation_features["features"]["operation_count"] == 1.0
        assert operation_features["features"]["job_count.aborted.scheduled.user_request"] == 1.0
        assert operation_features["tags"]["total_job_count"] == 1


class TestJobStatisticFeatures(YTEnvSetup):
    NUM_SCHEDULERS = 1
    USE_PORTO = True

    @authors("alexkolodezny")
    def test_job_statistic_features(self):
        create("table", "//tmp/t1")
        create("table", "//tmp/t2")
        write_table("//tmp/t1", [{"a": "b"}])
        op = map(
            in_="//tmp/t1",
            out="//tmp/t2",
            command='cat; bash -c "for (( I=0 ; I<=100*1000 ; I++ )) ; do echo $(( I+I*I )); done; sleep 2" >/dev/null',
        )

        def get_features(op):
            features = get(op.get_path() + "/@controller_features")
            operation_features = features[0]
            task_features = features[1]
            if "task_name" in operation_features["tags"]:
                task_features, operation_features = operation_features, task_features
            return operation_features, task_features

        features = get_features(op)[1]["features"]
        for component in ["user_job", "job_proxy"]:
            print_debug(component)
            assert features["job_statistics." + component + ".cpu.user.completed.sum"] > 0
            assert features["job_statistics." + component + ".cpu.system.completed.sum"] > 0
            assert features["job_statistics." + component + ".cpu.context_switches.completed.sum"] is not None
            assert features["job_statistics." + component + ".cpu.peak_thread_count.completed.max"] is not None
            assert features["job_statistics." + component + ".cpu.wait.completed.sum"] is not None
            assert features["job_statistics." + component + ".cpu.throttled.completed.sum"] is not None
            assert features["job_statistics." + component + ".block_io.bytes_read.completed.sum"] is not None
            assert features["job_statistics." + component + ".max_memory.completed.sum"] > 0

            assert features["job_statistics." + component + ".cpu.user.completed.avg"] > 0
            assert features["job_statistics." + component + ".cpu.system.completed.avg"] > 0
            assert features["job_statistics." + component + ".cpu.context_switches.completed.avg"] is not None
            assert features["job_statistics." + component + ".cpu.peak_thread_count.completed.max"] is not None
            assert features["job_statistics." + component + ".cpu.wait.completed.avg"] is not None
            assert features["job_statistics." + component + ".cpu.throttled.completed.avg"] is not None
            assert features["job_statistics." + component + ".block_io.bytes_read.completed.avg"] is not None
            assert features["job_statistics." + component + ".max_memory.completed.avg"] > 0

        assert features["job_statistics.user_job.cumulative_memory_mb_sec.completed.sum"] > 0
        assert features["job_statistics.user_job.cumulative_memory_mb_sec.completed.avg"] > 0


class TestListOperationFilterExperiments(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_SCHEDULERS = 1
    NUM_CONTROLLER_AGENTS = 1

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "experiments": {
                "exp_a1": {
                    "fraction": 0.4,
                    "ticket": "ytexp-1",
                    "ab_treatment_group": {
                        "fraction": 0.5,
                    },
                },
                "exp_b1": {
                    "fraction": 0.4,
                    "ticket": "ytexp-2",
                    "ab_treatment_group": {
                        "fraction": 0.5,
                    },
                },
                "exp_c1": {
                    "fraction": 0.1,
                    "ticket": "ytexp-2",
                    "ab_treatment_group": {
                        "fraction": 0.5,
                    },
                },
            },
        },
    }

    @authors("alexkolodezny")
    def test_list_operation_filter(self):
        create("table", "//tmp/t_in")

        op1 = map(in_="//tmp/t_in",
                  out=[],
                  command="cat",
                  spec={"experiment_overrides": ["exp_a1.treatment"]})

        op2 = map(in_="//tmp/t_in",
                  out=[],
                  command="cat",
                  spec={"experiment_overrides": ["exp_b1.treatment"]})

        op3 = map(in_="//tmp/t_in",
                  out=[],
                  command="cat",
                  spec={"experiment_overrides": ["exp_c1.treatment"]})

        res = list_operations(filter="exp_a1.treatment")
        assert {op["id"] for op in res["operations"]} == {op1.id}
        res = list_operations(filter="exp_b1.treatment")
        assert {op["id"] for op in res["operations"]} == {op2.id}
        res = list_operations()
        assert {op["id"] for op in res["operations"]} == {op1.id, op2.id, op3.id}


class TestPendingTimeFeatures(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_SCHEDULERS = 1
    NUM_CONTROLLER_AGENTS = 1
    NUM_NODES = 3

    DELTA_CONTROLLER_AGENT_CONFIG = {
        "controller_agent": {
            "snapshot_period": 500,
        }
    }

    @authors("alexkolodezny")
    def test_pending_time_features(self):
        nodes = ls("//sys/cluster_nodes")
        assert len(nodes) > 2
        for i in range(2):
            set("//sys/cluster_nodes/{}/@disable_scheduler_jobs".format(nodes[i]), True)

        op = vanilla(
            spec={
                "tasks": {
                    "task1": {
                        "job_count": 1,
                        "command": with_breakpoint("sleep 2; BREAKPOINT; sleep 2;"),
                    },
                },
            },
            track=False,
        )
        wait_breakpoint()
        op.wait_for_fresh_snapshot()

        with Restarter(self.Env, CONTROLLER_AGENTS_SERVICE):
            time.sleep(10)

        op.ensure_running()
        release_breakpoint()

        op.track()

        def get_time(op):
            res = get(op.get_path() + "/@controller_features")
            ready_time = 0
            exhaust_time = 0
            wall_time = 0
            for task in res:
                if "task_name" in task["tags"]:
                    ready_time += task["features"]["ready_time"]
                    exhaust_time += task["features"]["exhaust_time"]
                    wall_time += task["features"]["wall_time"]
            return ready_time, exhaust_time, wall_time

        ready_time, exhaust_time, wall_time = get_time(op)

        assert ready_time < 500
        assert exhaust_time > 4000
        assert exhaust_time < 17000
        assert wall_time > 14000

        op = vanilla(
            spec={
                "tasks": {
                    "task1": {
                        "job_count": 2,
                        "command": "sleep 3",
                    },
                },
            },
        )

        ready_time, exhaust_time, wall_time = get_time(op)

        assert ready_time > 2000
        assert exhaust_time > 2000
        assert wall_time > 6000

        op = vanilla(
            spec={
                "tasks": {
                    "task1": {
                        "job_count": 1,
                        "command": "sleep 3",
                    },
                },
            },
        )

        ready_time, exhaust_time, wall_time = get_time(op)

        assert ready_time < 500
        assert exhaust_time > 3000
        assert wall_time > 3000

        op = vanilla(
            spec={
                "tasks": {
                    "task1": {
                        "job_count": 1,
                        "command": "sleep 3",
                    },
                    "task2": {
                        "job_count": 1,
                        "command": "sleep 3",
                    }
                },
                "resource_limits": {
                    "user_slots": 1,
                },
            },
        )

        ready_time, exhaust_time, wall_time = get_time(op)

        assert ready_time < 500
        assert exhaust_time > 6000
        assert wall_time > 6000
