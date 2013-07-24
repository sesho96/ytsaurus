#!/usr/bin/env python

import argparse
import contextlib
import errno
import fcntl
import glob
import itertools
import os
import os.path
import pprint
import re
import resource
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import traceback
import xml.etree.ElementTree as etree

################################################################################
# These methods are used to mark actual steps to be executed.
# See the rest of the file for an example of usage.

_build_steps = []
_cleanup_steps = []


def yt_register_build_step(func):
    """Registers a build step to perform."""
    _build_steps.append(func)


def yt_register_cleanup_step(func):
    """Registers a clean-up steps to perform."""
    _cleanup_steps.append(func)


################################################################################
# Here are actual steps. All the meaty guts are way below.

@yt_register_build_step
def prepare(options):
    os.environ["LANG"] = "en_US.UTF-8"
    os.environ["LC_ALL"] = "en_US.UTF-8"

    options.build_number = os.environ["BUILD_NUMBER"]
    options.build_vcs_number = os.environ["BUILD_VCS_NUMBER"]

    codename = run_captured(["lsb_release", "-c"])
    codename = re.sub(r"^Codename:\s*", "", codename)

    if codename not in ["lucid", "precise"]:
        raise RuntimeError("Unknown LSB distribution code name: {0}".format(codename))

    options.repository = "yandex-" + codename

    # Now determine the compiler.
    cc = run_captured(["which", options.cc])
    cxx = run_captured(["which", options.cxx])

    if not cc:
        raise RuntimeError("Failed to locate C compiler")

    if not cxx:
        raise RuntimeError("Failed to locate CXX compiler")

    options.cc = cc
    options.cxx = cxx

    if os.path.exists(options.working_directory) and options.clean_working_directory:
        teamcity_message("Cleaning working directory...", status="WARNING")
        shutil.rmtree(options.working_directory)
    mkdirp(options.working_directory)

    if os.path.exists(options.sandbox_directory) and options.clean_sandbox_directory:
        teamcity_message("Cleaning sandbox directory...", status="WARNING")
        shutil.rmtree(options.sandbox_directory)
    mkdirp(options.sandbox_directory)

    teamcity_message(pprint.pformat(options.__dict__))


@yt_register_build_step
def configure(options):
    run([
        "cmake",
        "-DCMAKE_INSTALL_PREFIX=/usr",
        "-DCMAKE_BUILD_TYPE=%s" % options.type,
        "-DCMAKE_COLOR_MAKEFILE:BOOL=OFF",
        "-DYT_BUILD_ENABLE_EXPERIMENTS:BOOL=ON",
        "-DYT_BUILD_ENABLE_TESTS:BOOL=ON",
        "-DYT_BUILD_ENABLE_NODEJS:BOOL=ON",
        "-DYT_BUILD_BRANCH=%s" % options.branch,
        "-DYT_BUILD_NUMBER=%s" % options.build_number,
        "-DYT_BUILD_TAG=%s" % options.build_vcs_number[0:7],
        options.checkout_directory],
        cwd=options.working_directory,
        env={"CC": options.cc, "CXX": options.cxx})


@yt_register_build_step
def fast_build(options):
    cpus = int(os.sysconf("SC_NPROCESSORS_ONLN"))
    try:
        run(["make", "-j%d" % cpus], cwd=options.working_directory, silent=True)
    except ChildHasNonZeroExitCode:
        teamcity_message("(ignoring child failure to provide meaningful diagnostics in `slow_build`)")


@yt_register_build_step
def slow_build(options):
    run(["make"], cwd=options.working_directory)


@yt_register_build_step
def set_suid_bit(options):
    path = "%s/bin/ytserver" % options.working_directory
    run(["sudo", "chown", "root", path])
    run(["sudo", "chmod", "4755", path])


@yt_register_build_step
def package(options):
    if not options.package:
        return

    with cwd(options.working_directory):
        run(["make", "package"])
        run(["make", "version"])

        with open("ytversion") as handle:
            version = handle.read().strip()

        teamcity_interact("setParameter", name="yt.package_version", value=version)

        teamcity_message("We have built a package")
        teamcity_interact("setParameter", name="yt.package_built", value=1)

        artifacts = glob.glob("./ARTIFACTS/yandex-yt*{0}*.changes".format(version))
        if artifacts:
            run(["dupload", "--to", options.repository, "--nomail"] + artifacts)
            teamcity_message("We have uploaded a package")
            teamcity_interact("setParameter", name="yt.package_uploaded", value=1)


@yt_register_build_step
def run_prepare(options):
    with cwd(options.checkout_directory):
        run(["make", "-C", "./python/yt/wrapper"])
        run(["make", "-C", "./python", "version"])

    with cwd("%s/yt/nodejs" % options.working_directory):
        if os.path.exists("node_modules"):
            shutil.rmtree("node_modules")
        run(["npm", "install"])


@yt_register_build_step
def run_unit_tests(options):
    run([
        "gdb",
        "--batch",
        "--return-child-result",
        "--command=%s/scripts/teamcity-gdb-script" % options.checkout_directory,
        "--args",
        "./bin/unittester",
        "--gtest_color=no",
        "--gtest_output=xml:%s/gtest_unittester.xml" % options.working_directory],
        cwd=options.working_directory)


@yt_register_build_step
def run_javascript_tests(options):
    run(
        ["./run_tests.sh", "-R", "xunit"],
        cwd="%s/yt/nodejs" % options.working_directory,
        env={"MOCHA_OUTPUT_FILE": "%s/junit_nodejs_run_tests.xml" % options.working_directory})


def run_python_tests(options, suite_name, suite_path):
    sandbox_current = os.path.join(options.sandbox_directory, suite_name)
    sandbox_archive = os.path.join(
        os.path.expanduser("~/failed_tests/"),
        "_".join([options.btid, options.build_number, suite_name]))

    mkdirp(sandbox_current)

    failed = False
    result = None

    with tempfile.NamedTemporaryFile() as handle:
        try:
            run([
                "py.test",
                "-r", "x",
                "--verbose",
                "--exitfirst",
                "--capture=no",
                "--tb=native",
                "--timeout=300",
                "--junitxml=%s" % handle.name],
                cwd=suite_path,
                env={
                    "PATH": "{0}/bin:{0}/yt/nodejs:{1}".format(options.working_directory, os.environ.get("PATH", "")),
                    "PYTHONPATH": "{0}/python:{1}".format(options.checkout_directory, os.environ.get("PYTHONPATH", "")),
                    "TESTS_SANDBOX": sandbox_current
                })
        except ChildHasNonZeroExitCode:
            teamcity_message("(ignoring child failure since we are reading test results from XML)")
            failed = True

        result = etree.parse(handle)

    for node in (result.iter() if hasattr(result, "iter") else result.getiterator()):
        if isinstance(node.text, str):
            node.text = node.text \
                .replace("&quot;", "\"") \
                .replace("&apos;", "\'") \
                .replace("&amp;", "&") \
                .replace("&lt;", "<") \
                .replace("&gt;", ">")

    with open("%s/junit_python_%s.xml" % (options.working_directory, suite_name), "w+b") as handle:
        result.write(handle, encoding="utf-8")

    if failed:
        shutil.copytree(sandbox_current, sandbox_archive)
        raise RuntimeError("Propagating test failure")


@yt_register_build_step
def run_integration_tests(options):
    run_python_tests(options, "integration", "%s/tests/integration" % options.checkout_directory)


@yt_register_build_step
def run_python_libraries_tests(options):
    run_python_tests(options, "python_libraries", "%s/python" % options.checkout_directory)


@yt_register_cleanup_step
def clean_artifacts(options, n=10):
    for path in ls(
        "%s/ARTIFACTS" % options.working_directory,
        reverse=True,
        select=os.path.isfile,
        start=n,
        stop=sys.maxint):
            teamcity_message("Removing {0}...".format(path), status="WARNING")
            shutil.rmtree(path)


@yt_register_cleanup_step
def clean_failed_tests(options, n=5):
    for path in ls(
        os.path.expanduser("~/failed_tests/"),
        reverse=True,
        select=os.path.isdir,
        start=n,
        stop=sys.maxint):
            teamcity_message("Removing {0}...".format(path), status="WARNING")
            shutil.rmtree(path)


################################################################################
# Below are meaty guts. Be warned.

################################################################################
################################################################################
#     *                             )                         (      ____
#   (  `          (       *   )  ( /(   (               *   ) )\ )  |   /
#   )\))(   (     )\    ` )  /(  )\())  )\ )       (  ` )  /((()/(  |  /
#  ((_)()\  )\ ((((_)(   ( )(_))((_)\  (()/(       )\  ( )(_))/(_)) | /
#  (_()((_)((_) )\ _ )\ (_(_())__ ((_)  /(_))_  _ ((_)(_(_())(_))   |/
#  |  \/  || __|(_)_\(_)|_   _|\ \ / / (_)) __|| | | ||_   _|/ __| (
#  | |\/| || _|  / _ \    | |   \ V /    | (_ || |_| |  | |  \__ \ )\
#  |_|  |_||___|/_/ \_\   |_|    |_|      \___| \___/   |_|  |___/((_)
#
################################################################################
################################################################################

################################################################################
# These methods are used to interact with TeamCity.
# See http://confluence.jetbrains.com/display/TCD8/Build+Script+Interaction+with+TeamCity

def teamcity_escape(s):
    s = re.sub("(['\\[\\]|])", "|\\1", s)
    s = s.replace("\n", "|n").replace("\r", "|r")
    return s


def teamcity_interact(*args, **kwargs):
    r = " ".join(itertools.chain(
        (str(x) for x in args),
        ("{0}='{1}'".format(str(k), teamcity_escape(str(v))) for k, v in kwargs.iteritems())))
    r = "##teamcity[" + r + "]\n"
    sys.stdout.flush()
    sys.stderr.write(r)
    sys.stderr.flush()


def teamcity_message(text, status="NORMAL"):
    if status not in ["NORMAL", "WARNING", "FAILURE", "ERROR"]:
        raise ValueError("Invalid |status|: {0}".format(status))

    if status == "NORMAL":
        teamcity_interact("message", text=text)
    else:
        teamcity_interact("message", text=text, status=status)


@contextlib.contextmanager
def teamcity_block(name):
    try:
        teamcity_interact("blockOpened", name=name)
        yield
    finally:
        teamcity_interact("blockClosed", name=name)


@contextlib.contextmanager
def teamcity_step(name, funcname):
    now = time.time()

    try:
        teamcity_message("Executing: {0}".format(name))
        with teamcity_block(name):
            yield
        teamcity_message("Completed: {0}".format(name))
    except:
        teamcity_interact(
            "message",
            text="Caught exception; failing...".format(name),
            errorDetails=traceback.format_exc(),
            status="ERROR")
        teamcity_message("Failed: {0}".format(name), status="FAILURE")
        raise
    finally:
        teamcity_interact("buildStatisticValue", key=funcname, value=int(1000.0 * (time.time() - now)))


@contextlib.contextmanager
def cwd(new_path):
    try:
        old_path = os.getcwd()
        teamcity_message("Changing current directory to {0}".format(new_path))
        os.chdir(new_path)
        yield
    finally:
        teamcity_message("Changing current directory to {0}".format(old_path))
        os.chdir(old_path)


def ls(path, reverse=True, select=None, start=0, stop=None):
    if not os.path.isdir(path):
        return
    iterable = os.listdir(path)
    iterable = map(lambda x: os.path.realpath(os.path.join(path, x)), iterable)
    iterable = sorted(iterable, key=lambda x: os.stat(x).st_mtime, reverse=reverse)
    iterable = itertools.ifilter(select, iterable)
    iterable = itertools.islice(iterable, start, stop)
    for item in iterable:
        yield item


def mkdirp(path):
    try:
        os.makedirs(path)
    except OSError as ex:
        if ex.errno != errno.EEXIST:
            raise


_signals = dict((k, v) for v, k in signal.__dict__.iteritems() if v.startswith("SIG"))


class ChildKeepsRunningInIsolation(Exception):
    pass


class ChildHasNonZeroExitCode(Exception):
    pass


def run_captured(args, cwd=None, env=None, input=None):
    if env:
        tmp = os.environ.copy()
        tmp.update(env)
        env = tmp

    child = subprocess.Popen(
        args,
        bufsize=1,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=cwd,
        env=env)

    return child.communicate(input)[0].strip()  # This mimics bash $() behaviour.


def run_preexec():
    resource.setrlimit(resource.RLIMIT_CORE, (resource.RLIM_INFINITY, resource.RLIM_INFINITY))


def run(args, cwd=None, env=None, silent=False):
    POLL_TIMEOUT = 1.0
    POLL_ITERATIONS = 5
    READ_SIZE = 4096

    with teamcity_block("({0})".format(args[0])):
        saved_env = env
        if saved_env:
            tmp = os.environ.copy()
            tmp.update(saved_env)
            env = tmp

        child = subprocess.Popen(
            args,
            bufsize=0,
            stdin=None,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            preexec_fn=run_preexec,
            close_fds=True,
            cwd=cwd,
            env=env)

        teamcity_message("run({0}) => {1}".format(
            pprint.pformat({"args": args, "cwd": cwd, "env": saved_env}),
            child.pid))

        # Since we are doing non-blocking read, we have to deal with splitting
        # by ourselves. See http://bugs.python.org/issue1175#msg56041.
        evmask_read = select.POLLIN | select.POLLPRI
        evmask_error = select.POLLHUP | select.POLLERR | select.POLLNVAL

        poller = select.poll()
        poller.register(child.stdout, evmask_read | evmask_error)
        poller.register(child.stderr, evmask_read | evmask_error)

        # Holds the data from incomplete read()s.
        data_for = {
            child.stdout.fileno(): "",
            child.stderr.fileno(): ""
        }

        # Holds the message status.
        status_for = {
            child.stdout.fileno(): "NORMAL",
            child.stderr.fileno(): "WARNING"
        }

        # Switch FDs to non-blocking mode.
        for fd in [child.stdout.fileno(), child.stderr.fileno()]:
            fl = fcntl.fcntl(fd, fcntl.F_GETFL)
            fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

        def consume(fd, eof):
            # Try to read as much as possible from the FD.
            result = ""
            try:
                while True:
                    delta = os.read(fd, READ_SIZE)
                    if len(delta) > 0:
                        result += delta
                    else:
                        eof = True
                        break
            except OSError as e:
                if e.errno not in [errno.EAGAIN, errno.EWOULDBLOCK]:
                    raise

            # Emit complete lines from the buffer.
            data = data_for[fd] + result
            i = 0
            j = -1
            while True:
                j = data.find("\n", i)
                if j < 0:
                    break
                if not silent:
                    teamcity_message(data[i:j], status_for[fd])
                i = j + 1
            data_for[fd] = data[i:]

            # Emit incomplete lines from the buffer when there is no more data.
            if eof and len(data_for[fd]) > 0:
                if not silent:
                    teamcity_message(data_for[fd], status_for[fd])
                data_for[fd] = ""

        # Poll while we have alive FDs.
        while len(data_for) > 0:
            for fd, event in poller.poll(POLL_TIMEOUT):
                if event & evmask_read:
                    consume(fd, False)
                if event & evmask_error:
                    consume(fd, True)
                    poller.unregister(fd)
                    del data_for[fd]
                    del status_for[fd]

        # Await for the child to terminate.
        for i in xrange(POLL_ITERATIONS):
            if child.poll() is None:
                teamcity_message("Child is still running.", "WARNING")
                time.sleep(POLL_TIMEOUT)
            else:
                break

        if child.returncode is None:
            teamcity_message("Child is still running; killing it.", "WARNING")
            child.kill()
            raise ChildKeepsRunningInIsolation()

        if child.returncode < 0:
            teamcity_message(
                "Child was terminated by signal {0}".format(_signals[-child.returncode]),
                "FAILURE")

        if child.returncode > 0:
            teamcity_message(
                "Child has exited with return code {0}".format(child.returncode),
                "FAILURE")

        if child.returncode == 0:
            teamcity_message("Child has exited successfully")
        else:
            raise ChildHasNonZeroExitCode(str(child.returncode))


################################################################################
# This is an entry-point. Just boiler-plate.

def main():
    def parse_bool(s):
        if s == "YES":
            return True
        if s == "NO":
            return False
        raise argparse.ArgumentTypeError("Expected YES or NO")
    parser = argparse.ArgumentParser(description="YT Build Script")

    parser.add_argument("--btid", type=str, action="store", required=True)
    parser.add_argument("--branch", type=str, action="store", required=True)

    parser.add_argument(
        "--checkout_directory", metavar="DIR",
        type=str, action="store", required=True)

    parser.add_argument(
        "--working_directory", metavar="DIR",
        type=str, action="store", required=True)
    parser.add_argument(
        "--clean_working_directory",
        type=parse_bool, action="store", default=False)

    parser.add_argument(
        "--sandbox_directory", metavar="DIR",
        type=str, action="store", required=True)
    parser.add_argument(
        "--clean_sandbox_directory",
        type=parse_bool, action="store", default=False)

    parser.add_argument(
        "--type",
        type=str, action="store", required=True, choices=("Debug", "Release", "RelWithDebInfo"))

    parser.add_argument(
        "--package",
        type=parse_bool, action="store", default=False)

    parser.add_argument(
        "--cc",
        type=str, action="store", required=False, default="gcc-4.7")
    parser.add_argument(
        "--cxx",
        type=str, action="store", required=False, default="g++-4.7")

    options = parser.parse_args()
    status = 0

    try:
        for step in _build_steps:
            with teamcity_step("Build Step '{0}'".format(step.func_name), step.func_name):
                step(options)
    except:
        teamcity_message("Terminating...", status="FAILURE")
        status = 42
    finally:
        for step in _cleanup_steps:
            try:
                with teamcity_step("Clean-up Step '{0}'".format(step.func_name), step.func_name):
                    step(options)
            except:
                teamcity_interact(
                    "message",
                    text="Caught exception during cleanup phase; ignoring...",
                    errorDetails=traceback.format_exc(),
                    status="ERROR")
        teamcity_message("Done!")
        sys.exit(status)


if __name__ == "__main__":
    main()
