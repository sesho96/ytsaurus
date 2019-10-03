import pytest
from time import sleep
from operator import itemgetter
from copy import deepcopy
from flaky import flaky

from yt_env_setup import YTEnvSetup, wait
from yt_commands import *

##################################################################

class TestAccounts(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 16

    REPLICATOR_REACTION_TIME = 3.5

    def _replicator_sleep(self):
        sleep(self.REPLICATOR_REACTION_TIME)

    def _get_disk_space_for_medium(self, disk_space_map, medium_name = "default"):
        return disk_space_map.get(medium_name, 0)

    def _get_account_node_count(self, account):
        return get("//sys/accounts/{0}/@resource_usage/node_count".format(account))

    def _get_account_chunk_count(self, account):
        return get("//sys/accounts/{0}/@resource_usage/chunk_count".format(account))

    def _get_account_node_count_limit(self, account):
        return get("//sys/accounts/{0}/@resource_limits/node_count".format(account))

    def _set_account_node_count_limit(self, account, value):
        set("//sys/accounts/{0}/@resource_limits/node_count".format(account), value)

    def _get_account_chunk_count_limit(self, account):
        return get("//sys/accounts/{0}/@resource_limits/chunk_count".format(account))

    def _set_account_chunk_count_limit(self, account, value):
        set("//sys/accounts/{0}/@resource_limits/chunk_count".format(account), value)

    def _set_account_zero_limits(self, account):
        set("//sys/accounts/{0}/@resource_limits".format(account), {
            "disk_space_per_medium": {"default" : 0},
            "chunk_count": 0,
            "node_count": 0,
            "tablet_count": 0,
            "tablet_static_memory": 0
        })

    def _is_account_disk_space_limit_violated(self, account):
        return get("//sys/accounts/{0}/@violated_resource_limits/disk_space".format(account))

    def _is_account_node_count_limit_violated(self, account):
        return get("//sys/accounts/{0}/@violated_resource_limits/node_count".format(account))

    def _is_account_chunk_count_limit_violated(self, account):
        return get("//sys/accounts/{0}/@violated_resource_limits/chunk_count".format(account))

    def _get_tx_disk_space(self, tx, account):
        disk_space = get("#{0}/@resource_usage/{1}/disk_space_per_medium".format(tx, account))
        return self._get_disk_space_for_medium(disk_space, "default")

    def _get_tx_chunk_count(self, tx, account):
        return get("#{0}/@resource_usage/{1}/chunk_count".format(tx, account))

    @authors("babenko", "ignat")
    def test_init(self):
        assert sorted(ls("//sys/accounts")) == sorted(["sys", "tmp", "intermediate", "chunk_wise_accounting_migration"])
        assert get("//@account") == "sys"
        assert get("//sys/@account") == "sys"
        assert get("//tmp/@account") == "tmp"

    @authors("ignat")
    def test_account_create1(self):
        create_account("max")
        assert sorted(ls("//sys/accounts")) == sorted(["sys", "tmp", "intermediate", "chunk_wise_accounting_migration", "max"])
        assert get_account_disk_space("max") == 0
        assert self._get_account_node_count("max") == 0
        assert self._get_account_chunk_count("max") == 0

    @authors("babenko", "ignat")
    def test_account_create2(self):
        with pytest.raises(YtError): create_account("sys")
        with pytest.raises(YtError): create_account("tmp")

    @authors("babenko", "ignat")
    def test_account_remove_builtin(self):
        with pytest.raises(YtError): remove_account("sys")
        with pytest.raises(YtError): remove_account("tmp")

    @authors("babenko", "ignat")
    def test_account_create3(self):
        create_account("max")
        with pytest.raises(YtError): create_account("max")

    @authors("babenko", "ignat")
    def test_empty_name_fail(self):
        with pytest.raises(YtError): create_account("")

    @authors("babenko", "ignat")
    def test_account_attr1(self):
        set("//tmp/a", {})
        assert get("//tmp/a/@account") == "tmp"

    @authors("babenko")
    def test_account_attr2(self):
        # should not crash
        get("//sys/accounts/tmp/@")

    @authors("ignat")
    def test_account_attr3(self):
        set("//tmp/a", {"x" : 1, "y" : 2})
        assert get("//tmp/a/@account") == "tmp"
        assert get("//tmp/a/x/@account") == "tmp"
        assert get("//tmp/a/y/@account") == "tmp"
        copy("//tmp/a", "//tmp/b")
        assert get("//tmp/b/@account") == "tmp"
        assert get("//tmp/b/x/@account") == "tmp"
        assert get("//tmp/b/y/@account") == "tmp"

    @authors("ignat", "shakurov")
    def test_account_attr4(self):
        create_account("max")
        assert self._get_account_node_count("max") == 0
        assert self._get_account_chunk_count("max") == 0
        assert get_account_disk_space("max") == 0

        create("table", "//tmp/t")
        write_table("//tmp/t", {"a" : "b"})
        table_disk_space = get_chunk_owner_disk_space("//tmp/t")

        self._replicator_sleep()
        tmp_node_count = self._get_account_node_count("tmp")
        tmp_chunk_count = self._get_account_chunk_count("tmp")
        tmp_disk_space = get_account_disk_space("tmp")

        set("//tmp/t/@account", "max")

        self._replicator_sleep()

        assert self._get_account_node_count("tmp") == tmp_node_count - 1
        assert self._get_account_chunk_count("tmp") == tmp_chunk_count - 1
        assert get_account_disk_space("tmp") == tmp_disk_space - table_disk_space

        assert self._get_account_node_count("max") == 1
        assert self._get_account_chunk_count("max") == 1
        assert get_account_disk_space("max") == table_disk_space

    @authors("babenko", "ignat")
    def test_account_attr5(self):
        create_account("max")
        set("//tmp/a", {})
        tx = start_transaction()
        with pytest.raises(YtError): set("//tmp/a/@account", "max", tx=tx)

    @authors("babenko", "ignat")
    def test_remove_immediately(self):
        create_account("max")
        remove_account("max")
        wait(lambda: not exists("//sys/accounts/max"))

    @authors("babenko", "ignat")
    def test_remove_delayed(self):
        create_account("max")
        set("//tmp/a", {})
        set("//tmp/a/@account", "max")
        remove_account("max", sync_deletion=False)
        assert get("//sys/accounts/max/@life_stage") == "removal_started"
        remove("//tmp/a")
        wait(lambda: not exists("//sys/accounts/max"))

    @authors("babenko")
    def test_file1(self):
        wait(lambda: get_account_disk_space("tmp") == 0)

        create("file", "//tmp/f1")
        write_file("//tmp/f1", "some_data")

        wait(lambda: get_account_disk_space("tmp") > 0)
        space = get_account_disk_space("tmp")
        assert space > 0

        create("file", "//tmp/f2")
        write_file("//tmp/f2", "some_data")

        wait(lambda: get_account_disk_space("tmp") == 2 * space)

        remove("//tmp/f1")

        wait(lambda: get_account_disk_space("tmp") == space)

        remove("//tmp/f2")

        wait(lambda: get_account_disk_space("tmp") == 0)

    @authors("babenko")
    def test_file2(self):
        create("file", "//tmp/f")
        write_file("//tmp/f", "some_data")

        self._replicator_sleep()
        space = get_account_disk_space("tmp")

        create_account("max")
        set("//tmp/f/@account", "max")

        self._replicator_sleep()
        assert get_account_disk_space("tmp") == 0
        assert get_account_disk_space("max") == space

        remove("//tmp/f")

        gc_collect()
        self._replicator_sleep()
        assert get_account_disk_space("max") == 0

    @authors("babenko", "ignat", "shakurov")
    def test_file3(self):
        create_account("max")

        assert get_account_disk_space("max") == 0

        create("file", "//tmp/f", attributes={"account": "max"})
        write_file("//tmp/f", "some_data")

        self._replicator_sleep()
        assert get_account_disk_space("max") > 0

        remove("//tmp/f")

        gc_collect()
        self._replicator_sleep()
        assert get_account_disk_space("max") == 0

    @authors("babenko")
    def test_file4(self):
        create_account("max")

        create("file", "//tmp/f", attributes={"account": "max"})
        write_file("//tmp/f", "some_data")

        self._replicator_sleep()
        space = get_account_disk_space("max")
        assert space > 0

        rf  = get("//tmp/f/@replication_factor")
        set("//tmp/f/@replication_factor", rf * 2)

        self._replicator_sleep()
        assert get_account_disk_space("max") == space * 2

    @authors("shakurov")
    def test_table1(self):
        node_count = self._get_account_node_count("tmp")

        create("table", "//tmp/t")
        write_table("//tmp/t", {"a" : "b"})

        self._replicator_sleep()
        assert get_account_disk_space("tmp") > 0
        assert self._get_account_node_count("tmp") == node_count + 1
        assert self._get_account_chunk_count("tmp") == 1

    @authors("babenko")
    def test_table2(self):
        create("table", "//tmp/t")

        tx = start_transaction(timeout=60000)
        for i in xrange(0, 5):
            write_table("//tmp/t", {"a" : "b"}, tx=tx)

            self._replicator_sleep()
            ping_transaction(tx)

            account_space = get_account_disk_space("tmp")
            tx_space = self._get_tx_disk_space(tx, "tmp")

            assert get_account_committed_disk_space("tmp") == 0
            assert account_space > 0
            assert account_space == tx_space
            assert get_chunk_owner_disk_space("//tmp/t") == 0
            assert get_chunk_owner_disk_space("//tmp/t", tx=tx) == tx_space
            last_space = tx_space

        commit_transaction(tx)

        self._replicator_sleep()
        assert get_chunk_owner_disk_space("//tmp/t") == last_space

    @authors("babenko")
    def test_table3(self):
        create("table", "//tmp/t")
        write_table("//tmp/t", {"a" : "b"})

        self._replicator_sleep()
        space1 = get_account_disk_space("tmp")
        assert space1 > 0

        tx = start_transaction(timeout=60000)
        write_table("//tmp/t", {"xxxx" : "yyyy"}, tx=tx)

        self._replicator_sleep()
        space2 = self._get_tx_disk_space(tx, "tmp")
        assert space1 != space2
        assert get_account_disk_space("tmp") == space1 + space2
        assert get_chunk_owner_disk_space("//tmp/t") == space1
        assert get_chunk_owner_disk_space("//tmp/t", tx=tx) == space2

        commit_transaction(tx)

        self._replicator_sleep()
        assert get_account_disk_space("tmp") == space2
        assert get_chunk_owner_disk_space("//tmp/t") == space2

    @authors("babenko")
    def test_table4(self):
        tx = start_transaction(timeout=60000)
        create("table", "//tmp/t", tx=tx)
        write_table("//tmp/t", {"a" : "b"}, tx=tx)

        self._replicator_sleep()
        assert get_account_disk_space("tmp") > 0

        abort_transaction(tx)

        self._replicator_sleep()
        assert get_account_disk_space("tmp") == 0

    @authors("ignat")
    def test_table5(self):
        tmp_node_count = self._get_account_node_count("tmp")
        tmp_chunk_count = self._get_account_chunk_count("tmp")

        create("table", "//tmp/t")
        write_table("//tmp/t", {"a" : "b"})

        self._replicator_sleep()
        assert self._get_account_node_count("tmp") == tmp_node_count + 1
        assert self._get_account_chunk_count("tmp") == tmp_chunk_count + 1
        space = get_account_disk_space("tmp")
        assert space > 0

        create_account("max")

        set("//tmp/t/@account", "max")

        self._replicator_sleep()
        assert self._get_account_node_count("tmp") == tmp_node_count
        assert self._get_account_chunk_count("tmp") == tmp_chunk_count
        assert self._get_account_node_count("max") == 1
        assert self._get_account_chunk_count("max") == 1
        assert get_account_disk_space("tmp") == 0
        assert get_account_disk_space("max") == space

        set("//tmp/t/@account", "tmp")

        self._replicator_sleep()
        assert self._get_account_node_count("tmp") == tmp_node_count + 1
        assert self._get_account_chunk_count("tmp") == tmp_chunk_count + 1
        assert self._get_account_node_count("max") == 0
        assert self._get_account_chunk_count("max") == 0
        assert get_account_disk_space("tmp") == space
        assert get_account_disk_space("max") == 0

    @authors("sandello")
    def test_table6(self):
        create("table", "//tmp/t")

        tx = start_transaction(timeout=60000)
        write_table("//tmp/t", {"a" : "b"}, tx=tx)

        self._replicator_sleep()
        space = get_chunk_owner_disk_space("//tmp/t", tx=tx)
        assert space > 0
        assert get_account_disk_space("tmp") == space

        tx2 = start_transaction(tx=tx, timeout=60000)

        self._replicator_sleep()
        assert get_chunk_owner_disk_space("//tmp/t", tx=tx2) == space

        write_table("<append=true>//tmp/t", {"a" : "b"}, tx=tx2)

        self._replicator_sleep()
        assert get_chunk_owner_disk_space("//tmp/t", tx=tx2) == space * 2
        assert get_account_disk_space("tmp") == space * 2

        commit_transaction(tx2)

        self._replicator_sleep()
        assert get_chunk_owner_disk_space("//tmp/t", tx=tx) == space * 2
        assert get_account_disk_space("tmp") == space * 2
        commit_transaction(tx)

        self._replicator_sleep()
        assert get_chunk_owner_disk_space("//tmp/t") == space * 2
        assert get_account_disk_space("tmp") == space * 2

    @authors("ignat")
    def test_node_count_limits1(self):
        create_account("max")
        assert not self._is_account_node_count_limit_violated("max")
        self._set_account_node_count_limit("max", 1000)
        self._set_account_node_count_limit("max", 2000)
        self._set_account_node_count_limit("max", 0)
        assert not self._is_account_node_count_limit_violated("max")
        with pytest.raises(YtError): self._set_account_node_count_limit("max", -1)

    @authors("babenko", "ignat")
    def test_node_count_limits2(self):
        create_account("max")
        assert self._get_account_node_count("max") == 0

        create("table", "//tmp/t")
        set("//tmp/t/@account", "max")

        self._replicator_sleep()
        assert self._get_account_node_count("max") == 1

    @authors("babenko")
    def test_node_count_limits3(self):
        create_account("max")
        create("table", "//tmp/t")

        self._set_account_node_count_limit("max", 0)

        self._replicator_sleep()
        with pytest.raises(YtError): set("//tmp/t/@account", "max")

    @authors("babenko")
    def test_node_count_limits4(self):
        create_account("max")
        create("table", "//tmp/t")
        write_table("//tmp/t", {"a" : "b"})

        self._set_account_node_count_limit("max", 0)
        with pytest.raises(YtError): set("//tmp/t/@account", "max")

    @authors("shakurov")
    def test_node_count_limits5(self):
        create_account("max")
        create("map_node", "//tmp/a")
        set("//tmp/a/@account", "max")
        create("table", "//tmp/a/t1")
        write_table("//tmp/a/t1", {"a" : "b"})

        multicell_sleep()

        node_count = self._get_account_node_count("max")
        self._set_account_node_count_limit("max", node_count)

        multicell_sleep()

        # Shouldn't work 'cause node count usage is checked synchronously.
        with pytest.raises(YtError): copy("//tmp/a/t1", "//tmp/a/t2")

    @authors("ignat")
    def test_chunk_count_limits1(self):
        create_account("max")
        assert not self._is_account_chunk_count_limit_violated("max")
        self._set_account_chunk_count_limit("max", 1000)
        self._set_account_chunk_count_limit("max", 2000)
        self._set_account_chunk_count_limit("max", 0)
        assert not self._is_account_chunk_count_limit_violated("max")
        with pytest.raises(YtError): wait(lambda: self._set_account_chunk_count_limit("max", -1))

    @authors("babenko", "ignat")
    def test_chunk_count_limits2(self):
        create_account("max")
        assert self._get_account_chunk_count("max") == 0

        create("table", "//tmp/t")
        write_table("//tmp/t", {"a" : "b"})
        set("//tmp/t/@account", "max")

        self._replicator_sleep()
        assert self._get_account_chunk_count("max") == 1

    @authors("shakurov")
    def test_chunk_count_limits3(self):
        create_account("max")
        create("map_node", "//tmp/a")
        set("//tmp/a/@account", "max")
        create("table", "//tmp/a/t1")
        write_table("//tmp/a/t1", {"a" : "b"})

        self._set_account_chunk_count_limit("max", 1)

        copy("//tmp/a/t1", "//tmp/a/t2")
        self._replicator_sleep()

        assert self._get_account_chunk_count("max") == 1

        create("table", "//tmp/t")
        write_table("//tmp/t", {"a" : "b"})

        copy("//tmp/t", "//tmp/a/t3", pessimistic_quota_check=False)
        self._replicator_sleep()
        # After a requisition update, max's chunk count usage should've increased.

        assert self._get_account_chunk_count("max") == 2
        create("table", "//tmp/a/t4")
        with pytest.raises(YtError): wait(lambda: write_table("//tmp/a/t4", {"a" : "b"}))

    @authors("ignat")
    def test_disk_space_limits1(self):
        create_account("max")
        assert not self._is_account_disk_space_limit_violated("max")
        set_account_disk_space_limit("max", 1000)
        set_account_disk_space_limit("max", 2000)
        set_account_disk_space_limit("max", 0)
        assert not self._is_account_disk_space_limit_violated("max")
        with pytest.raises(YtError): wait(lambda: set_account_disk_space_limit("max", -1))

    @authors("ignat")
    def test_disk_space_limits2(self):
        create_account("max")
        set_account_disk_space_limit("max", 1000000)

        create("table", "//tmp/t")
        set("//tmp/t/@account", "max")

        write_table("//tmp/t", {"a" : "b"})

        self._replicator_sleep()

        assert not self._is_account_disk_space_limit_violated("max")

        set_account_disk_space_limit("max", 0)
        self._replicator_sleep()

        assert self._is_account_disk_space_limit_violated("max")
        with pytest.raises(YtError): wait(lambda: write_table("//tmp/t", {"a" : "b"}))
        # Wait for upload tx to abort
        wait(lambda: get("//tmp/t/@locks") == [])

        set_account_disk_space_limit("max", get_account_disk_space("max") + 1)
        self._replicator_sleep()
        assert not self._is_account_disk_space_limit_violated("max")

        write_table("<append=true>//tmp/t", {"a" : "b"})

        self._replicator_sleep()
        assert self._is_account_disk_space_limit_violated("max")

    @authors("ignat")
    def test_disk_space_limits3(self):
        create_account("max")
        set_account_disk_space_limit("max", 1000000)

        create("file", "//tmp/f1", attributes={"account": "max"})
        write_file("//tmp/f1", "some_data")

        self._replicator_sleep()
        assert not self._is_account_disk_space_limit_violated("max")

        set_account_disk_space_limit("max", 0)
        assert self._is_account_disk_space_limit_violated("max")

        create("file", "//tmp/f2", attributes={"account": "max"})
        with pytest.raises(YtError): wait(lambda: write_file("//tmp/f2", "some_data"))

        set_account_disk_space_limit("max", get_account_disk_space("max") + 1)
        assert not self._is_account_disk_space_limit_violated("max")

        create("file", "//tmp/f3", attributes={"account": "max"})
        write_file("//tmp/f3", "some_data")

        self._replicator_sleep()
        assert self._is_account_disk_space_limit_violated("max")

    @authors("shakurov")
    def test_disk_space_limits4(self):
        create("map_node", "//tmp/a")
        create("file", "//tmp/a/f1")
        write_file("//tmp/a/f1", "some_data")
        create("file", "//tmp/a/f2")
        write_file("//tmp/a/f2", "some_data")

        self._replicator_sleep()
        disk_space = get_chunk_owner_disk_space("//tmp/a/f1")
        disk_space_2 = get_chunk_owner_disk_space("//tmp/a/f2")
        assert disk_space == disk_space_2

        create_account("max")
        create("map_node", "//tmp/b")
        set("//tmp/b/@account", "max")

        set_account_disk_space_limit("max", disk_space * 2)
        copy("//tmp/a", "//tmp/b/a")

        wait(lambda: get_account_disk_space("max") == disk_space * 2)
        assert exists("//tmp/b/a")

        multicell_sleep()

        def write_multiple_chunks_to_file():
            for i in xrange(20):
                write_file("//tmp/b/a/f3", "some_data {0}".format(i))

        create("file", "//tmp/b/a/f3")
        # Writing new data should fail...
        with pytest.raises(YtError): wait(write_multiple_chunks_to_file)

        # Wait for upload tx to abort
        wait(lambda: get("//tmp/b/a/f3/@locks") == [])
        remove("//tmp/b/a/f3")
        # ...but copying existing data should be ok...
        copy("//tmp/b/a/f2", "//tmp/b/a/f3")

        # ...and shouldn't increase disk space usage.
        wait(lambda: get_account_disk_space("max") == disk_space * 2)

        remove("//tmp/b/a")

        wait(lambda: get_account_disk_space("max") == 0 and self._get_account_node_count("max") == 1)

        assert not exists("//tmp/b/a")

    @authors("shakurov")
    def test_disk_space_limits5(self):
        create_account("max")
        create("map_node", "//tmp/a")
        set("//tmp/a/@account", "max")
        create("table", "//tmp/a/t1")
        write_table("//tmp/a/t1", {"a" : "b"})
        disk_space = get_chunk_owner_disk_space("//tmp/a/t1")

        set_account_disk_space_limit("max", disk_space)

        copy("//tmp/a/t1", "//tmp/a/t2")
        self._replicator_sleep()

        assert get_account_disk_space("max") == disk_space

        create("table", "//tmp/t")
        write_table("//tmp/t", {"a" : "b"})

        copy("//tmp/t", "//tmp/a/t3", pessimistic_quota_check=False)
        self._replicator_sleep()
        # After a requisition update, max's disk space usage should've increased.

        assert get_account_disk_space("max") == 2 * disk_space
        create("table", "//tmp/a/t4")
        with pytest.raises(YtError): wait(lambda: write_table("//tmp/a/t4", {"a" : "b"}))

    @authors("babenko")
    def test_committed_usage(self):
        self._replicator_sleep()
        assert get_account_committed_disk_space("tmp") == 0

        create("table", "//tmp/t")
        write_table("//tmp/t", {"a" : "b"})

        self._replicator_sleep()
        space = get_chunk_owner_disk_space("//tmp/t")
        assert space > 0
        assert get_account_committed_disk_space("tmp") == space

        tx = start_transaction(timeout=60000)
        write_table("<append=true>//tmp/t", {"a" : "b"}, tx=tx)

        self._replicator_sleep()
        assert get_account_committed_disk_space("tmp") == space

        commit_transaction(tx)

        self._replicator_sleep()
        assert get_account_committed_disk_space("tmp") == space * 2

    @authors("babenko")
    def test_nested_tx_uncommitted_usage(self):
        create("table", "//tmp/t")
        write_table("<append=true>//tmp/t", {"a" : "b"})
        write_table("<append=true>//tmp/t", {"a" : "b"})

        self._replicator_sleep()
        assert self._get_account_chunk_count("tmp") == 2

        tx1 = start_transaction(timeout=60000)
        tx2 = start_transaction(tx=tx1, timeout=60000)

        write_table("<append=true>//tmp/t", {"a" : "b"}, tx=tx2)

        self._replicator_sleep()
        assert self._get_account_chunk_count("tmp") == 3

        assert get("//tmp/t/@update_mode") == "none"
        assert get("//tmp/t/@update_mode", tx=tx1) == "none"
        assert get("//tmp/t/@update_mode", tx=tx2) == "append"

        assert self._get_tx_chunk_count(tx1, "tmp") == 0
        assert self._get_tx_chunk_count(tx2, "tmp") == 1

        commit_transaction(tx2)

        self._replicator_sleep()
        assert get("//tmp/t/@update_mode") == "none"
        assert get("//tmp/t/@update_mode", tx=tx1) == "append"
        assert self._get_account_chunk_count("tmp") == 3
        assert self._get_tx_chunk_count(tx1, "tmp") == 1

        commit_transaction(tx1)

        self._replicator_sleep()
        assert get("//tmp/t/@update_mode") == "none"
        assert self._get_account_chunk_count("tmp") == 3

    @authors("babenko", "ignat")
    def test_copy(self):
        create_account("a1")
        create_account("a2")

        create("map_node", "//tmp/x1", attributes={"account": "a1"})
        assert get("//tmp/x1/@account") == "a1"

        create("map_node", "//tmp/x2", attributes={"account": "a2"})
        assert get("//tmp/x2/@account") == "a2"

        create("table", "//tmp/x1/t")
        assert get("//tmp/x1/t/@account") == "a1"

        write_table("//tmp/x1/t", {"a" : "b"})

        self._replicator_sleep()
        space = get_account_disk_space("a1")
        assert space > 0
        assert space == get_account_committed_disk_space("a1")

        copy("//tmp/x1/t", "//tmp/x2/t")
        assert get("//tmp/x2/t/@account") == "a2"

        self._replicator_sleep()
        assert space == get_account_disk_space("a2")
        assert space == get_account_committed_disk_space("a2")

    @authors("shakurov")
    def test_chunk_wise_accounting1(self):
        create_medium("hdd2")
        create_medium("hdd3")
        create_account("a")

        tmp_node_count = self._get_account_node_count("tmp")
        node_count = self._get_account_node_count("a")

        # 1) Just basic accounting.

        create("table", "//tmp/t1")
        set("//tmp/t1/@media/default/replication_factor", 1)

        write_table("//tmp/t1", {"a" : "b"})

        chunk_size = get_chunk_owner_disk_space("//tmp/t1")

        media = get("//tmp/t1/@media")
        media["default"]["replication_factor"] = 3
        media["hdd2"] = {"replication_factor": 4, "data_parts_only": True}
        set("//tmp/t1/@media", media)
        self._replicator_sleep()

        tmp_resource_usage = {"node_count": tmp_node_count + 1, "chunk_count": 1, "disk_space_per_medium": {"default": 3*chunk_size, "hdd2": 4*chunk_size}}
        self._check_resource_usage("tmp", tmp_resource_usage)

        # 2) Chunks shared among accounts should be charged to both, but with different factors.

        create("map_node", "//tmp/a")
        set("//tmp/a/@account", "a")

        with pytest.raises(YtError): copy("//tmp/t1", "//tmp/a/t1")
        set_account_disk_space_limit("a", 100000, "hdd2")
        copy("//tmp/t1", "//tmp/a/t1")
        self._replicator_sleep()

        resource_usage = {"node_count": node_count + 2, "chunk_count": 1, "disk_space_per_medium": {"default": 3*chunk_size, "hdd2": 4*chunk_size}}
        self._check_resource_usage("tmp", tmp_resource_usage)
        self._check_resource_usage("a", resource_usage)

        del media["default"]
        media["hdd2"]["replication_factor"] = 2
        media["hdd3"] = {"replication_factor": 5, "data_parts_only": False}
        set("//tmp/a/t1/@primary_medium", "hdd3")
        set("//tmp/a/t1/@media", media)
        resource_usage["disk_space_per_medium"]["default"] = 0
        resource_usage["disk_space_per_medium"]["hdd2"] = 2*chunk_size
        resource_usage["disk_space_per_medium"]["hdd3"] = 5*chunk_size
        self._replicator_sleep()

        self._check_resource_usage("tmp", tmp_resource_usage)
        self._check_resource_usage("a", resource_usage)

        # 3) Copying chunks you already own isn't charged - unless the copy requires higher replication factor.

        copy("//tmp/a/t1", "//tmp/a/t2")
        resource_usage["node_count"] += 1
        self._replicator_sleep()

        self._check_resource_usage("tmp", tmp_resource_usage)
        self._check_resource_usage("a", resource_usage)

        assert get("//tmp/a/t1/@media") == get("//tmp/a/t2/@media")

        # Add a new medium,..
        media["default"] = {"replication_factor": 2, "data_parts_only": False}
        # ...increase RF on another medium (this should make a difference),..
        media["hdd2"]["replication_factor"] = 3
        # ...and decrease on yet another one (this shouldn't make a difference).
        media["hdd3"]["replication_factor"] = 4
        set("//tmp/a/t2/@media", media)
        resource_usage["disk_space_per_medium"]["default"] = 2*chunk_size
        resource_usage["disk_space_per_medium"]["hdd2"] = 3*chunk_size
        self._replicator_sleep()

        self._check_resource_usage("tmp", tmp_resource_usage)
        self._check_resource_usage("a", resource_usage)

        # 4) Basic transaction accounting - committing.

        tx = start_transaction(timeout=60000)
        create("table", "//tmp/a/t3")
        committed_resource_usage = deepcopy(resource_usage)
        committed_resource_usage["node_count"] += 1
        resource_usage["node_count"] += 2
        write_table("//tmp/a/t3", {"a" : "b"}, tx=tx)
        resource_usage["chunk_count"] += 1
        resource_usage["disk_space_per_medium"]["default"] += 3*chunk_size
        self._replicator_sleep()

        tx_resource_usage = get("//sys/transactions/{0}/@resource_usage".format(tx))
        assert tx_resource_usage["a"]["node_count"] == 1
        assert tx_resource_usage["a"]["chunk_count"] == 1
        assert tx_resource_usage["a"]["disk_space_per_medium"].get("default", 0) == 3*chunk_size

        self._check_resource_usage("a", resource_usage)
        self._check_committed_resource_usage("a", committed_resource_usage)

        commit_transaction(tx)
        resource_usage["node_count"] -= 1
        self._replicator_sleep()

        self._check_resource_usage("a", resource_usage)
        self._check_committed_resource_usage("a", resource_usage)

        # 5) Basic accounting with some additional data.

        set("//tmp/a/t3/@media",
            {"default": {"replication_factor": 2, "data_parts_only": False},\
             "hdd2": {"replication_factor": 3, "data_parts_only": True}})
        resource_usage["disk_space_per_medium"]["default"] -= chunk_size
        resource_usage["disk_space_per_medium"]["hdd2"] += 3*chunk_size
        self._replicator_sleep()

        self._check_resource_usage("a", resource_usage)

        # 6) Transaction accounting - aborting.

        tx = start_transaction(timeout=60000)
        create("table", "//tmp/a/t4")
        committed_resource_usage = deepcopy(resource_usage)
        committed_resource_usage["node_count"] += 1
        resource_usage["node_count"] += 2
        write_table("//tmp/a/t4", {"a" : "b"}, tx=tx)
        resource_usage["chunk_count"] += 1
        resource_usage["disk_space_per_medium"]["default"] += 3*chunk_size
        self._replicator_sleep()

        tx_resource_usage = get("//sys/transactions/{0}/@resource_usage".format(tx))
        assert tx_resource_usage["a"]["node_count"] == 1
        assert tx_resource_usage["a"]["chunk_count"] == 1
        assert tx_resource_usage["a"]["disk_space_per_medium"].get("default", 0) == 3*chunk_size

        self._check_resource_usage("a", resource_usage)
        self._check_committed_resource_usage("a", committed_resource_usage)

        abort_transaction(tx)
        self._replicator_sleep()

        self._check_resource_usage("a", committed_resource_usage)
        self._check_committed_resource_usage("a", committed_resource_usage)
        resource_usage = deepcopy(committed_resource_usage)

        # 7) Appending.

        write_table("<append=true>//tmp/a/t3", {"a" : "b"})
        resource_usage["chunk_count"] += 1
        resource_usage["disk_space_per_medium"]["default"] += 2*chunk_size
        resource_usage["disk_space_per_medium"]["hdd2"] += 3*chunk_size
        self._replicator_sleep()

        self._check_resource_usage("a", resource_usage)

    @authors("shakurov")
    def test_chunk_wise_accounting2(self):
        create_medium("hdd4")
        create_medium("hdd5")
        create_account("a")

        tmp_node_count = self._get_account_node_count("tmp")
        node_count = self._get_account_node_count("a")

        codec = "reed_solomon_6_3"
        codec_data_ratio = 6.0/9.0

        # 1) Basic erasure-aware accounting.

        create("table", "//tmp/t1")
        set("//tmp/t1/@erasure_codec", codec)
        write_table("//tmp/t1", {"a" : "b"})

        chunk_size = get_chunk_owner_disk_space("//tmp/t1")

        media = get("//tmp/t1/@media")
        media["default"]["replication_factor"] = 3
        media["hdd4"] = {"replication_factor": 1, "data_parts_only": True}
        set("//tmp/t1/@media", media)
        self._replicator_sleep()

        tmp_resource_usage = {"node_count": tmp_node_count + 1, "chunk_count": 1, "disk_space_per_medium": {"default": chunk_size, "hdd4": int(codec_data_ratio * chunk_size)}}
        self._check_resource_usage("tmp", tmp_resource_usage)

        create("map_node", "//tmp/a")
        set("//tmp/a/@account", "a")

        # 1) Sharing chunks.

        with pytest.raises(YtError): copy("//tmp/t1", "//tmp/a/t1")
        set_account_disk_space_limit("a", 100000, "hdd4")
        copy("//tmp/t1", "//tmp/a/t1")
        self._replicator_sleep()

        resource_usage = {"node_count": node_count + 2, "chunk_count": 1, "disk_space_per_medium": {"default": chunk_size, "hdd4": int(codec_data_ratio * chunk_size)}}
        self._check_resource_usage("tmp", tmp_resource_usage)
        self._check_resource_usage("a", resource_usage)

        # 2) Sharing chunks within single account.

        copy("//tmp/a/t1", "//tmp/a/t2")
        resource_usage["node_count"] += 1
        self._replicator_sleep()

        self._check_resource_usage("tmp", tmp_resource_usage)
        self._check_resource_usage("a", resource_usage)

        media["hdd5"] = {"replication_factor": 5, "data_parts_only": False}
        set("//tmp/a/t2/@media", media)
        resource_usage["disk_space_per_medium"]["hdd5"] = chunk_size
        self._replicator_sleep()

        self._check_resource_usage("tmp", tmp_resource_usage)
        self._check_resource_usage("a", resource_usage)


    def _check_resource_usage(self, account, resource_usage):
        self._check_resource_usage_impl(account, resource_usage, False)

    def _check_committed_resource_usage(self, account, resource_usage):
        self._check_resource_usage_impl(account, resource_usage, True)

    def _check_resource_usage_impl(self, account, resource_usage, committed):
        actual_resource_usage = get("//sys/accounts/{0}/@{1}resource_usage".format(account, "committed_" if committed else ""))
        assert actual_resource_usage["node_count"] == resource_usage["node_count"]
        assert actual_resource_usage["chunk_count"] == resource_usage["chunk_count"]
        for medium, disk_space in resource_usage["disk_space_per_medium"].iteritems():
            assert actual_resource_usage["disk_space_per_medium"].get(medium, 0) == disk_space

    @authors("babenko")
    def test_move_preserve_account_success(self):
        # setup
        create_account("a")
        set_account_disk_space_limit("a", 100000)
        create("map_node", "//tmp/x")
        set("//tmp/x/@account", "a")
        create("table", "//tmp/x/t")
        write_table("//tmp/x/t", {"a" : "b"})

        # make "a" overcommitted
        self._set_account_zero_limits("a")

        # move must succeed
        move("//tmp/x", "//tmp/y", preserve_account=True)

    @authors("babenko")
    def test_move_dont_preserve_account_success(self):
        # setup
        create_account("a")
        set_account_disk_space_limit("a", 100000)
        create("map_node", "//tmp/x")
        set("//tmp/x/@account", "a")
        create("table", "//tmp/x/t")
        write_table("//tmp/x/t", {"a" : "b"})
        create("map_node", "//tmp/for_y")
        set("//tmp/for_y/@account", "a")

        # make "a" overcommitted
        self._set_account_zero_limits("a")

        # move must succeed
        move("//tmp/x", "//tmp/for_y/y", preserve_account=False)

    @authors("babenko")
    def test_move_dont_preserve_account_fail(self):
        # setup
        create("map_node", "//tmp/x")
        create_account("a")
        create("map_node", "//tmp/for_y")
        set("//tmp/for_y/@account", "a")

        # make "a" overcommitted
        self._set_account_zero_limits("a")

        # move must fail
        with pytest.raises(YtError): move("//tmp/x", "//tmp/for_y/y", preserve_account=False)

    @authors("babenko")
    def test_copy_preserve_account_fail(self):
        # setup
        create_account("a")
        create("map_node", "//tmp/x")
        set("//tmp/x/@account", "a")

        # make "a" overcommitted
        self._set_account_zero_limits("a")

        # copy must fail
        with pytest.raises(YtError): copy("//tmp/x", "//tmp/y", preserve_account=True)

    @authors("babenko")
    def test_copy_dont_preserve_account_fail(self):
        # setup
        create_account("a")
        create("map_node", "//tmp/x")
        create("map_node", "//tmp/for_y")
        set("//tmp/x/@account", "a")
        set("//tmp/for_y/@account", "a")

        # make "a" overcommitted
        self._set_account_zero_limits("a")

        # copy must fail
        with pytest.raises(YtError): copy("//tmp/x", "//tmp/for_y/y", preserve_account=False)

    @authors("babenko", "ignat")
    def test_rename_success(self):
        create_account("a1")
        set("//sys/accounts/a1/@name", "a2")
        assert get("//sys/accounts/a2/@name") == "a2"

    @authors("babenko", "ignat")
    def test_rename_fail(self):
        create_account("a1")
        create_account("a2")
        with pytest.raises(YtError): set("//sys/accounts/a1/@name", "a2")


    @authors("babenko")
    def test_set_account_fail_yt_6207(self):
        create_account("a")
        create("table", "//tmp/t")
        write_table("//tmp/t", {"a" : "b"})
        assert get("//tmp/t/@account") == "tmp"
        self._replicator_sleep()
        assert get("//sys/accounts/tmp/@resource_usage/disk_space") > 0
        assert get("//sys/accounts/a/@resource_usage/disk_space") == 0
        create_user("u")
        with pytest.raises(YtError):  set("//tmp/t/@account", "a", authenticated_user="u")
        self._replicator_sleep()
        assert get("//sys/accounts/tmp/@resource_usage/disk_space") > 0
        assert get("//sys/accounts/a/@resource_usage/disk_space") == 0


    @authors("babenko")
    def test_change_account_with_snapshot_lock(self):
        self._replicator_sleep()
        tmp_nc = get("//sys/accounts/tmp/@resource_usage/node_count")
        create("table", "//tmp/t")
        create_account("a")
        self._replicator_sleep()
        assert get("//sys/accounts/a/@ref_counter") == 1
        assert get("//sys/accounts/tmp/@resource_usage/node_count") == tmp_nc + 1
        assert get("//sys/accounts/a/@resource_usage/node_count") == 0
        tx = start_transaction()
        lock("//tmp/t", mode="snapshot", tx=tx)
        self._replicator_sleep()
        assert get("//sys/accounts/a/@ref_counter") == 1
        assert get("//sys/accounts/tmp/@resource_usage/node_count") == tmp_nc + 2
        assert get("//sys/accounts/a/@resource_usage/node_count") == 0
        set("//tmp/t/@account", "a")
        self._replicator_sleep()
        assert get("//sys/accounts/a/@ref_counter") == 3
        assert get("//sys/accounts/tmp/@resource_usage/node_count") == tmp_nc + 1
        assert get("//sys/accounts/a/@resource_usage/node_count") == 1
        abort_transaction(tx)
        self._replicator_sleep()
        assert get("//sys/accounts/tmp/@resource_usage/node_count") == tmp_nc
        assert get("//sys/accounts/a/@resource_usage/node_count") == 1
        assert get("//sys/accounts/a/@ref_counter") == 3


    @authors("babenko")
    def test_regular_disk_usage(self):
        create("table", "//tmp/t")
        set("//tmp/t/@replication_factor", 5)
        write_table("//tmp/t", {"a" : "b"})
        chunk_list_id = get("//tmp/t/@chunk_list_id")
        assert get("//tmp/t/@resource_usage/disk_space_per_medium/default") == \
               get("#{0}/@statistics/regular_disk_space".format(chunk_list_id)) * 5

    @authors("babenko")
    def test_erasure_disk_usage(self):
        create("table", "//tmp/t")
        set("//tmp/t/@erasure_codec", "lrc_12_2_2")
        set("//tmp/t/@replication_factor", 5)
        write_table("//tmp/t", {"a" : "b"})
        chunk_list_id = get("//tmp/t/@chunk_list_id")
        assert get("//tmp/t/@resource_usage/disk_space_per_medium/default") == \
               get("#{0}/@statistics/erasure_disk_space".format(chunk_list_id))


    @authors("babenko")
    def test_create_with_invalid_attrs_yt_7093(self):
        with pytest.raises(YtError):
            create_account("x", attributes={"resource_limits": 123})
        assert not exists("//sys/accounts/x")

    @authors("shakurov")
    def test_requisitions(self):
        create_medium("hdd6")
        create_account("a")

        create("table", "//tmp/t")
        write_table("//tmp/t", {"a" : "b"})

        chunk_id = get_singular_chunk_id("//tmp/t")

        self._replicator_sleep()

        requisition = get("#" + chunk_id + "/@requisition")
        assert len(requisition) == 1
        assert requisition[0] == {
            "account" : "tmp",
            "medium" : "default",
            "replication_policy" : {"replication_factor" : 3, "data_parts_only" : False},
            "committed" : True
        }

        # Link the chunk to another table...
        copy("//tmp/t", "//tmp/t2")
        set("//tmp/t2/@account", "a")

        # ...and modify the original table's properties in some way.
        tbl_media = get("//tmp/t/@media")
        tbl_media["hdd6"] = {"replication_factor": 7, "data_parts_only" : True}
        tbl_media["default"] = {"replication_factor": 4, "data_parts_only" : False}
        set("//tmp/t/@media", tbl_media)

        self._replicator_sleep()

        requisition = get("#" + chunk_id + "/@requisition")
        requisition = sorted(requisition, key=itemgetter("account", "medium"))
        assert requisition == [
            {
                "account" : "a",
                "medium" : "default",
                "replication_policy" : {"replication_factor" : 3, "data_parts_only" : False},
                "committed" : True
            },
            {
                "account" : "tmp",
                "medium" : "default",
                "replication_policy" : {"replication_factor" : 4, "data_parts_only" : False},
                "committed" : True
            },
            {
                "account" : "tmp",
                "medium" : "hdd6",
                "replication_policy" : {"replication_factor" : 7, "data_parts_only" : True},
                "committed" : True
            }
        ]

    @authors("shakurov")
    def test_inherited_account_override_yt_8391(self):
        create_account("a1")
        create_account("a2")

        create_user("u1")
        create_user("u2")

        set("//sys/accounts/a1/@acl", [make_ace("allow", "u1", "use")])
        set("//sys/accounts/a2/@acl", [make_ace("allow", "u2", "use")])

        with pytest.raises(YtError):
            create("map_node", "//tmp/dir1", attributes={"account": "a1"}, authenticated_user="u2")

        create("map_node", "//tmp/dir1", attributes={"account": "a1"}, authenticated_user="u1")

        with pytest.raises(YtError):
            create("map_node", "//tmp/dir1/dir2", authenticated_user="u2")

        create("map_node", "//tmp/dir1/dir2", attributes={"account": "a2"}, authenticated_user="u2")

        assert get("//tmp/dir1/@account") == "a1"
        assert get("//tmp/dir1/dir2/@account") == "a2"

    @authors("shakurov")
    def test_recursive_create_with_explicit_account(self):
        create_account("a")
        create("document", "//tmp/one/two/three", recursive=True, attributes={"account": "a"})
        assert get("//tmp/@account") == "tmp"
        assert get("//tmp/one/two/three/@account") == "a"
        assert get("//tmp/one/two/@account") == "a"
        assert get("//tmp/one/@account") == "a"

    @authors("shakurov")
    def test_nested_tx_copy(self):
        create("table", "//tmp/t")

        multicell_sleep()
        node_count = get("//sys/accounts/tmp/@resource_usage/node_count")
        committed_node_count = get("//sys/accounts/tmp/@committed_resource_usage/node_count")

        tx1 = start_transaction()
        copy("//tmp/t", "//tmp/t1", tx=tx1)

        node_count += 3 # one for branched map node, one for cloned table, one for branched cloned table
        committed_node_count += 1 # one for cloned table
        multicell_sleep()
        assert get("//sys/accounts/tmp/@resource_usage/node_count") == node_count
        assert get("//sys/accounts/tmp/@committed_resource_usage/node_count") == committed_node_count

        commit_transaction(tx1)

        # Transaction changes disappear...
        node_count -= 3
        committed_node_count -= 1
        # but the newly committed node remains.
        node_count += 1
        committed_node_count += 1
        multicell_sleep()
        gc_collect()
        assert get("//sys/accounts/tmp/@resource_usage/node_count") == node_count
        assert get("//sys/accounts/tmp/@committed_resource_usage/node_count") == committed_node_count

    @authors("shakurov")
    def test_branched_nodes_not_checked_yt_8551(self):
        create("table", "//tmp/t")

        multicell_sleep()
        node_count = get("//sys/accounts/tmp/@resource_usage/node_count")
        committed_node_count = get("//sys/accounts/tmp/@committed_resource_usage/node_count")

        tx1 = start_transaction()
        copy("//tmp/t", "//tmp/t1", tx=tx1)

        node_count += 3 # one for branched map node, one for cloned table, one for branched cloned table
        committed_node_count += 1 # one for cloned table
        wait(lambda: get("//sys/accounts/tmp/@resource_usage/node_count") == node_count)
        wait(lambda: get("//sys/accounts/tmp/@committed_resource_usage/node_count") == committed_node_count)

        copy("//tmp/t", "//tmp/t2", tx=tx1)

        node_count += 2 # one for cloned table, one for branched cloned table
        committed_node_count += 1 # one for cloned table
        wait(lambda: get("//sys/accounts/tmp/@resource_usage/node_count") == node_count)
        wait(lambda: get("//sys/accounts/tmp/@committed_resource_usage/node_count") == committed_node_count)

        self._set_account_node_count_limit("tmp", committed_node_count)
        with pytest.raises(YtError): copy("//tmp/t", "//tmp/t3", tx=tx1)

        self._set_account_node_count_limit("tmp", committed_node_count + 1)
        copy("//tmp/t", "//tmp/t3", tx=tx1)

        node_count += 2
        committed_node_count += 1
        wait(lambda: get("//sys/accounts/tmp/@resource_usage/node_count") == node_count)
        wait(lambda: get("//sys/accounts/tmp/@committed_resource_usage/node_count") == committed_node_count)

        self._set_account_node_count_limit("tmp", node_count + 2)
        copy("//tmp/t", "//tmp/t4", tx=tx1)

        node_count += 2
        committed_node_count += 1
        wait(lambda: get("//sys/accounts/tmp/@resource_usage/node_count") == node_count)
        wait(lambda: get("//sys/accounts/tmp/@committed_resource_usage/node_count") == committed_node_count)

    @authors("shakurov")
    @flaky(max_runs=3)
    def test_totals(self):
        self._set_account_zero_limits("chunk_wise_accounting_migration")

        def add_resources(*resources):
            result = {
                "disk_space_per_medium": {"default": 0},
                "disk_space": 0,
                "chunk_count": 0,
                "node_count": 0,
                "tablet_count": 0,
                "tablet_static_memory": 0
            }
            for r in resources:
                result["disk_space_per_medium"]["default"] += r["disk_space_per_medium"].get("default", 0)
                result["disk_space"] += r["disk_space"]
                result["chunk_count"] += r["chunk_count"]
                result["node_count"] += r["node_count"]
                result["tablet_count"] += r["tablet_count"]
                result["tablet_static_memory"] += r["tablet_static_memory"]

            return result

        def resources_equal(a, b):
            return (a["disk_space_per_medium"].get("default", 0) == b["disk_space_per_medium"].get("default", 0) and
                    a["disk_space"] == b["disk_space"] and
                    a["chunk_count"] == b["chunk_count"] and
                    a["node_count"] == b["node_count"] and
                    a["tablet_count"] == b["tablet_count"] and
                    a["tablet_static_memory"] == b["tablet_static_memory"])

        resource_limits = get("//sys/accounts/@total_resource_limits")

        create_account("a1")
        set("//sys/accounts/a1/@resource_limits", {
            "disk_space_per_medium": {"default": 1000},
            "chunk_count": 1,
            "node_count": 1,
            "tablet_count": 0,
            "tablet_static_memory": 0
        })
        create_account("a2")
        set("//sys/accounts/a2/@resource_limits", {
            "disk_space_per_medium": {"default": 1000},
            "chunk_count": 1,
            "node_count": 1,
            "tablet_count": 0,
            "tablet_static_memory": 0
        })

        total_resource_limits = add_resources(
            resource_limits,
            {
                "disk_space_per_medium": {"default": 2000},
                "disk_space": 2000,
                "chunk_count": 2,
                "node_count": 2,
                "tablet_count": 0,
                "tablet_static_memory": 0
            })

        # A cleanup from preceding tests may still be happening in background. Wait until totals have stabilized.
        resource_usage = get("//sys/accounts/@total_resource_usage")
        committed_resource_usage = get("//sys/accounts/@total_committed_resource_usage")
        stable_iteration_count = 0
        for i in xrange(0, 30):
            sleep(0.3)
            new_resource_usage = get("//sys/accounts/@total_resource_usage")
            new_committed_resource_usage = get("//sys/accounts/@total_committed_resource_usage")
            if (resources_equal(resource_usage, new_resource_usage) and
                resources_equal(committed_resource_usage, new_committed_resource_usage)):
                stable_iteration_count += 1
                if stable_iteration_count == 10:
                    # Totals have been stable long enough, continue.
                    break
            else:
                resource_usage = new_resource_usage
                committed_resource_usage = new_committed_resource_usage
                stable_iteration_count = 0

        assert resources_equal(get("//sys/accounts/@total_resource_limits"), total_resource_limits)

        create("table", "//tmp/t1", attributes={"account": "a1"})
        create("table", "//tmp/t2", attributes={"account": "a2"})
        write_table("//tmp/t1", {"a" : "b"})
        write_table("//tmp/t2", {"c" : "d"})

        wait(lambda: get_account_disk_space("a1") > 0)
        wait(lambda: get_account_disk_space("a2") > 0)

        def totals_match():
            resource_usage1 = get("//sys/accounts/a1/@resource_usage")
            committed_resource_usage1 = get("//sys/accounts/a1/@committed_resource_usage")

            resource_usage2 = get("//sys/accounts/a2/@resource_usage")
            committed_resource_usage2 = get("//sys/accounts/a2/@committed_resource_usage")

            total_resource_usage = add_resources(resource_usage, resource_usage1, resource_usage2)
            total_committed_resource_usage = add_resources(committed_resource_usage, committed_resource_usage1, committed_resource_usage2)

            return (resources_equal(get("//sys/accounts/@total_resource_usage"), total_resource_usage) and
                    resources_equal(get("//sys/accounts/@total_committed_resource_usage"), total_committed_resource_usage))

        wait(totals_match)

##################################################################

class TestAccountsMulticell(TestAccounts):
    NUM_SECONDARY_MASTER_CELLS = 2
    NUM_SCHEDULERS = 1

    @authors("babenko")
    def test_requisitions2(self):
        create_account("a1")
        create_account("a2")

        create("table", "//tmp/t1", attributes={"account": "a1", "external_cell_tag": 1})
        write_table("//tmp/t1", {"a" : "b"})

        create("table", "//tmp/t2", attributes={"account": "a2", "external_cell_tag": 2})
        merge(mode="unordered",
              in_=["//tmp/t1", "//tmp/t1"],
              out="//tmp/t2")

        chunk_id = get_singular_chunk_id("//tmp/t1")

        self._replicator_sleep()

        requisition = get("#" + chunk_id + "/@requisition")
        assert len(requisition) == 2

        remove("//tmp/t1")

        wait(lambda: len(get("#" + chunk_id + "/@owning_nodes")) == 1)
        wait(lambda: len(get("#" + chunk_id + "/@requisition")) == 1)
