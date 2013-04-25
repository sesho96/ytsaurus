import yt.yson as yson
import simplejson as json

"""This module provides default ytserver configs"""

def get_master_config():
    return yson.parse_string(
"""
{
    meta_state = {
        cell = {
            addresses = [ ];
        };
        follower_tracker = {
            ping_interval = 3000;
        };
        changelogs = {
            path = "";
        };
        snapshots = {
            path = "";
        };
        max_changes_between_snapshots = 1000000;
        max_batch_delay = 0;
        leader_committer = {
            rpc_timeout = 10000;
        };
    };
  
    transaction_manager = {
        default_transaction_timeout = 300000;
    };

    chunk_manager = {
        chunk_refresh_delay = 700;
    };

    node_tracker = {
        online_node_timeout = 600;
    };

    logging = {
        rules = [
            {
                min_level = Info;
                writers = [ file ];
                categories  = [ "*" ];
            };
            {
                min_level = Debug;
                writers = [ raw ];
                categories  = [ "*" ];
            };
            {
                min_level = Error;
                writers = [ stderr ];
                categories  = [ "*" ];
            };
        ];
        writers = {
            stderr = {
                type = StdErr;
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            file = {
                type = File;
                file_name = "master-0.log";
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            raw = {
                type = raw;
                file_name = "master-0.debug.log";
            };
        }
  };
}
""")

def get_scheduler_config():
    return yson.parse_string(
"""
{
    masters = {
        addresses = [
            "localhost:9000";
        ];
    };

    scheduler = {
        strategy = fair_share;
        max_failed_job_count = 10;
        snapshot_period = 100000000;
        environment = {
             PYTHONUSERBASE = "/tmp"
        };
    };

    logging = {
        rules = [
            {
                min_level = Info;
                writers = [ file ];
                categories  = [ "*" ];
            };
            {
                min_level = Debug;
                writers = [ raw ];
                categories  = [ "*" ];
            };
            {
                min_level = Error;
                writers = [ stderr ];
                categories  = [ "*" ];
            };
        ];
        writers = {
            stderr = {
                type = StdErr;
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            file = {
                type = File;
                file_name = "scheduler-0.log";
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            raw = {
                type = raw;
                file_name = "scheduler-0.debug.log";
            };
        }
    }
}
""")


def get_driver_config():
    return yson.parse_string(
"""
{
    masters = {
        addresses = [
            "localhost";
        ];
        rpc_timeout = 30000;
    };

    logging = {
        rules = [
            {
                min_level = Info;
                writers = [ file ];
                categories  = [ "*" ];
            };
            {
                min_level = Debug;
                writers = [ file_debug ];
                categories  = [ "*" ];
            };
            {
                min_level = Error;
                writers = [ stderr ];
                categories  = [ "*" ];
            };
        ];
        writers = {
            stderr = {
                type = StdErr;
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            file = {
                type = File;
                file_name = "ytdriver.log";
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            file_debug = {
                type = File;
                file_name = "ytdriver.debug.log";
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
        };
    };
    "format_defaults" = {
        "structured" = <
            "format" = "text"
        > "yson";
        "tabular" = <
            "format" = "text"
        > "yson"
    };
    "operation_wait_timeout" = 3000;
    "transaction_manager" = {
        "ping_period" = 5000
    }
}
""")

def get_node_config():
    return yson.parse_string(
"""
{
    data_node = {
        cache_location = {
            path = "";
        };
        store_locations = [];
        heartbeat_period = 200;
    };

    exec_agent = {
        environment_manager = {
            environments = {
                default = {
                    type = unsafe;
                };
            };
        };

        job_controller = {
            resource_limits = {
                memory = 8000000000;
                slots = 1;
            };
        };

        slot_manager = {
            slot_location = "";
        };

        job_proxy_logging = {
            rules = [
                {
                    min_level = Info;
                    writers = [ file ];
                    categories  = [ "*" ];
                };
                {
                    min_level = Debug;
                    writers = [ raw ];
                    categories  = [ "*" ];
                };
                {
                    min_level = Error;
                    writers = [ stderr ];
                    categories  = [ "*" ];
                };
            ];
            writers = {
                stderr = {
                    type = StdErr;
                    pattern = "$(datetime) $(level) $(category) $(message)";
                };
                file = {
                    type = File;
                    file_name = "job_proxy-0.log";
                    pattern = "$(datetime) $(level) $(category) $(message)";
                };
                raw = {
                    type = Raw;
                    file_name = "job_proxy-0.debug.log";
                };
            }
        };
    };

    masters = {
        addresses = [];
        rpc_timeout = 5000
    };

    logging = {
        rules = [
            {
                min_level = info;
                writers = [ file ];
                categories  = [ "*" ];
            };
            {
                min_level = debug;
                writers = [ raw ];
                categories  = [ "*" ];
            };
        ];
        writers = {
            stderr = {
                type = std_err;
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            file = {
                type = file;
                file_name = "node-0.log";
                pattern = "$(datetime) $(level) $(category) $(message)";
            };
            raw = {
                type = raw;
                file_name = "node-0.debug.log";
            };
        }
    };
}
""")

def get_proxy_config():
    return json.loads(
"""
{
    "port" : -1,
    "log_port" : -1,
    "address" : "localhost",
    "number_of_workers" : 1,
    "memory_limit" : 33554432,
    "thread_limit" : 64,
    "spare_threads" : 4,

    "neighbours" : [  ],

    "logging" : {
        "filename" : "/dev/null"
    },

    "locals" : { },
    "disable_blackbox" : true,

    "proxy" : {
        "logging" : {
            "rules" : [ ],
            "writers" : { }
        },
        "driver" : { }
    }
}
""")
