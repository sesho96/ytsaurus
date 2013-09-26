#!/usr/bin/env python

from yt.tools.atomic import process_tasks_from_list
from yt.tools.common import update_args
from yt.tools.mr import Mr

import yt.logger as logger
import yt.wrapper as yt

import os
import copy

from argparse import ArgumentParser

def export_table(object, args):
    object = copy.deepcopy(object)
    if isinstance(object, dict):
        src = object["src"]
        del object["src"]
        dst = object["dst"]
        del object["dst"]
        params = update_args(args, object)
    else:
        src = object
        dst = os.path.join(params.destination_dir, src.strip("/"))
        params = args

    mr = Mr(binary=params.mapreduce_binary,
            server=params.mr_server,
            server_port=params.mr_server_port,
            http_port=params.mr_http_port,
            proxies=params.mr_proxy,
            proxy_port=params.mr_proxy_port,
            fetch_info_from_http=params.fetch_info_from_http,
            cache=False)

    logger.info("Exporting '%s' to '%s'", src, dst)

    if not yt.exists(src):
        logger.warning("Export table '%s' is empty", src)
        return -1

    if not mr.is_empty(dst):
        if params.force:
            mr.drop(dst)
        else:
            logger.error("Destination table '%s' is not empty" % dst)
            return -1

    record_count = yt.records_count(src)

    user_slots_path = "//sys/pools/{}/@resource_limits/user_slots".format(params.yt_pool)
    if not yt.exists(user_slots_path):
        logger.error("Use pool with bounded number of user slots")
    else:
        limit = params.speed_limit / yt.get(user_slots_path)

    command = "pv -q -L {} | "\
        "USER=tmp MR_USER={} {} -server {} -append -lenval -subkey -write {}"\
            .format(limit,
                    params.mr_user,
                    mr.binary,
                    mr.server,
                    dst)
    logger.info("Running map '%s'", command)
    yt.run_map(command, src, yt.create_temp_table(),
               files=mr.binary,
               format=yt.YamrFormat(has_subkey=True, lenval=True),
               memory_limit=2500 * yt.config.MB,
               spec={"pool": params.yt_pool,
                     "data_size_per_job": 2 * 1024 * yt.config.MB})

    result_record_count = mr.records_count(dst)
    if record_count != result_record_count:
        logger.error("Incorrect record count (expected: %d, actual: %d)", record_count, result_record_count)
        mr.drop(dst)
        return -1


def main():
    parser = ArgumentParser()
    parser.add_argument("--tables-queue")
    parser.add_argument("--destination-dir")

    parser.add_argument("--mr-server")
    parser.add_argument("--mr-server-port", default="8013")
    parser.add_argument("--mr-http-port", default="13013")
    parser.add_argument("--mr-proxy", action="append")
    parser.add_argument("--mr-proxy-port", default="13013")
    parser.add_argument("--mr-user", default="tmp")
    parser.add_argument("--mapreduce-binary", default="./mapreduce")
    parser.add_argument("--fetch-info-from-http", action="store_true", default=False)

    parser.add_argument("--speed-limit", type=int, default=500 * yt.config.MB)
    parser.add_argument("--force", action="store_true", default=False)
    parser.add_argument("--fastbone", action="store_true", default=False)

    parser.add_argument("--yt-pool", default="export_restricted")

    args = parser.parse_args()

    process_tasks_from_list(
        args.tables_queue,
        lambda obj: export_table(obj, args)
    )

if __name__ == "__main__":
    main()
