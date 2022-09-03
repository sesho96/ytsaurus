PACKAGE_NAME = "yandex-yt"

def main():
    from helpers import get_version, recursive, is_debian

    from setuptools import setup, find_packages

    import os
    import shutil

    version = get_version()
    version = version.split("-")[0]
    stable_versions = []
    if os.path.exists("stable_versions"):
        with open("stable_versions") as fin:
            stable_versions = fin.read().split("\n")

    # NB: version in package should be without alpha suffix.
    with open("yt/wrapper/version.py", "w") as version_output:
        version_output.write("VERSION='{0}'".format(version))

    if not is_debian and version not in stable_versions:
        version = version + "a1"

    yt_completion_destination = "yandex-yt-python/yt_completion"

    binaries = [
        "yt/wrapper/bin/mapreduce-yt",
        "yt/wrapper/bin/yt",
        "yt/wrapper/bin/yt-fuse",
    ]

    data_files = []
    scripts = binaries
    if is_debian:
        shutil.copy("yandex-yt-python/yt_completion", yt_completion_destination)
        data_files.append(("/etc/bash_completion.d/", [yt_completion_destination]))

    find_packages("yt/packages")
    setup(
        name=PACKAGE_NAME,
        version=version,
        packages=["yt", "yt.wrapper", "yt.yson", "yt.ypath", "yt.skiff", "yt.clickhouse", "yt.wrapper.schema"] + recursive("yt/packages"),
        package_dir={"yt.packages.certifi": "yt/packages/certifi"},
        package_data={"yt.packages.certifi": ["*.pem"],
                      "yt.wrapper": ["YandexInternalRootCA.crt"]},
        scripts=scripts,

        author="Ignat Kolesnichenko",
        author_email="ignat@yandex-team.ru",
        description="Python wrapper for YT system and yson parser.",
        keywords="yt python wrapper mapreduce yson",

        long_description=\
            "It is python library for YT system that works through http api " \
            "and supports most of the features. It provides a lot of default behaviour in case "\
            "of empty tables and absent paths. Also this package provides mapreduce binary "\
            "(based on python library) that is back compatible with Yamr system.",

        data_files=data_files
    )

    try:
        os.remove(yt_completion_destination)
    except OSError:
        pass

if __name__ == "__main__":
    main()
