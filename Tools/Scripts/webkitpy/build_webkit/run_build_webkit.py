# Copyright (C) 2020 Igalia S.L.
# -*- coding: utf-8 -*-
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

from __future__ import print_function
import logging
import optparse
import os
import subprocess
import sys
import time
import traceback
import argparse

from webkitpy.common.system.executive import Executive
from webkitpy.port import configuration_options, platform_options
from webkitcorepy.string_utils import elapsed

_log = logging.getLogger(__name__)

SOURCE_DIRECTORY = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))

def main(argv):
    option_parser = argparse.ArgumentParser(usage="%(prog)s [options]", add_help=False)
    groups = [("Platform options", platform_options()), ("Configuration options", configuration_options())]

    groups.append(("Build options", [
        optparse.make_option("--makeargs", action="append", default=[],
                             help=("Optional Makefile flags")),
        optparse.make_option("--cmakeargs", action="append", default=[],
                             help=("One or more optional CMake flags (e.g. --cmakeargs=\"-DFOO=bar -DCMAKE_PREFIX_PATH=/usr/local\"")),
        optparse.make_option("--no-ninja", action="store_true", default=False,
                             help=("Disable Ninja for CMake builds. In this case make will be used. Default: False")),
        optparse.make_option("--no-experimental-features", action="store_true", default=False,
                             help=("Disable experimental CMake features. Default: False")),
        optparse.make_option("--verbose", action="store_true", default=False,
                             help=("Enable verbose output"))
    ]))

    # Convert options to argparse, so that we can use parse_known_args() which is not supported in optparse.
    # FIXME: Globally migrate to argparse. https://bugs.webkit.org/show_bug.cgi?id=213463
    for group_name, group_options in groups:
        option_group = option_parser.add_argument_group(group_name)

        for option in group_options:
            # Skip deprecated option
            if option.get_opt_string() != "--target":
                default = None
                if option.default != ("NO", "DEFAULT"):
                    default = option.default

                kw = dict(action=option.action, dest=option.dest,help=option.help,default=default)
                if option.action != "store_true":
                    kw['const'] = option.const
                option_group.add_argument(option.get_opt_string(), **kw)

    options, args = option_parser.parse_known_args(argv)

    if not options.configuration:
        options.configuration = "Release"

    if set(args).issubset(["-h", "--help"]) and not options.platform:
        option_parser.print_help()
        print("\nTo see the available options on a specific platform, supply it on the command-line, for example --gtk --help")
        return 0

    try:
        return run(options, args, sys.stderr)
    except BaseException as e:
        if isinstance(e, Exception):
            print('\n%s raised: %s' % (e.__class__.__name__, str(e)), file=stderr)
            traceback.print_exc(file=stderr)
        return 1

def run(options, args, logging_stream):
    logger = logging.getLogger()
    logger.setLevel(logging.INFO)

    startTime = time.time()
    builder = Builder(options)
    exitCode = builder.run(args)
    buildTime = elapsed(time.time() - startTime)
    _log.debug("Build completed, Exit status: %d" % exitCode)
    if exitCode == 0:
        resultStr = "is now built! 🎉"
        extra = "\nTo run MiniBrowser with this newly-built code, use the\n\"Tools/Scripts/run-minibrowser --%s\" script." % options.platform
    else:
        resultStr = "build failed. 😟"
        extra = ""
    print("―" * 55)
    print("WebKit %s (build time: %s).%s" % (resultStr, buildTime, extra))
    print("―" * 55)
    return exitCode

class Builder:
    def __init__(self, options):
        self._options = options

    def numberOfCPUs(self):
        try:
            return int(os.environ["NUMBER_OF_PROCESSORS"])
        except:
            return Executive().cpu_count()

    def maxCPULoad(self):
        try:
            return int(os.environ["MAX_CPU_LOAD"])
        except:
            return None

    def run(self, args):
        if self._options.no_ninja or os.environ.get("NUMBER_OF_PROCESSORS"):
            minusJOverride = True
            for opt in self._options.makeargs:
                if opt.startswith("-j"):
                    minusJOverride = False
                    break
            if minusJOverride:
                self._options.makeargs.append("-j%d" % self.numberOfCPUs())

        minusLOverride = True
        for opt in self._options.makeargs:
            if opt.startswith("-l"):
                minusLOverride = False
                break
        if minusLOverride:
            maxCPULoad = self.maxCPULoad()
            if maxCPULoad is not None and maxCPULoad > 0:
                self._options.makeargs.append("-l%d" % maxCPULoad)

        return self._buildCMakeProject()

    def _cmakeArgsFromFeatures(self):
        args = []
        if not self._options.no_experimental_features:
            args.append("-DENABLE_EXPERIMENTAL_FEATURES=ON")

        # TODO: Add feature options

        return args

    def _mkdir(self, path):
        try:
            os.makedirs(path)
        except OSError:
            pass

    def _shouldRemoveCMakeCache(self, buildArgs):
        cachePath = os.path.join(self._buildDir(), "build-webkit-options.txt")
        contents = " ".join(buildArgs)

        def writeCache():
            with open(cachePath, "w") as f:
                f.write(contents)

        if not os.path.isfile(cachePath):
            self._mkdir(self._buildDir())
            writeCache()
            return True

        with open(cachePath) as f:
            options = set(f.read().strip().split())
            writeCache()
            if options != set(buildArgs):
                return True

        cacheFileModifiedTime = os.stat(cachePath).st_mtime

        for filename in ("OptionsCommon.cmake", "Options%s.cmake" % self._cmakePortName()):
            commonOptions = os.path.join(SOURCE_DIRECTORY, "Source", "cmake", filename)
            if os.path.isfile(commonOptions):
                mtime = os.stat(commonOptions).st_mtime
                if cacheFileModifiedTime < mtime:
                    return True

        return False

    def _cmakeCachePath(self):
        return os.path.join(self._buildDir(), "CMakeCache.txt")

    def _cmakeGeneratedBuildFile(self):
        if not self._options.no_ninja:
            filename = "build.ninja"
        else:
            filename = "Makefile"
        return os.path.join(self._buildDir(), filename)

    def _cmakePortName(self):
        # untested: win, playstation, win-cairo, ftw, apple-win, mac, jsc-only
        mapping = {"gtk": "GTK", "wpe": "WPE", "win": "Win", "playstation": "PlayStation", "win-cairo": "WinCairo",
                   "ftw": "FTW", "apple-win": "AppleWin", "mac": "Mac", "jsc-only": "JSCOnly"}
        return mapping.get(self._options.platform, "")

    def _baseProductDir(self):
        baseProductDir = os.environ.get("WEBKIT_OUTPUTDIR", SOURCE_DIRECTORY)
        return os.path.join(baseProductDir, "WebKitBuild")

    def _buildDir(self):
        return os.path.join(self._baseProductDir(), self._options.configuration)

    def execute(self, args, cwd=None, env=None):
        _log.debug(" ".join(args))
        return subprocess.call(args, cwd=cwd, env=env)

    def _asanEnabled(self):
        asan = os.path.join(self._buildDir(), "ASan")
        if not os.path.isfile(asan):
            return False

        with open(asan) as f:
            data = f.read()
            return data.strip().lower() == "yes"

    def _generateBuildSystemFromCMakeProject(self):
        port = self._cmakePortName()

        features = self._cmakeArgsFromFeatures()
        if self._shouldRemoveCMakeCache(features) and os.path.isfile(self._cmakeCachePath()):
            os.unlink(self._cmakeCachePath())

        # We try to be smart about when to rerun cmake, so that we can have faster incremental builds.
        if os.path.isfile(self._cmakeCachePath()) and os.path.isfile(self._cmakeGeneratedBuildFile()):
            return

        cmd = ["cmake", "-DPORT=%s" % port,
               "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
               "-DCMAKE_BUILD_TYPE=%s" % self._options.configuration,
               "-DDEVELOPER_MODE=ON"]
        if not self._options.no_ninja:
            cmd.append("-GNinja")

        if self._asanEnabled():
            cmd.append("-DENABLE_SANITIZERS=address")

        cmd.extend(features)
        cmd.extend(self._options.cmakeargs)
        cmd.append(SOURCE_DIRECTORY)
        #print(' '.join(cmd))
        build_path = self._buildDir()
        self._mkdir(build_path)
        self.execute(cmd, cwd=build_path)

    def _buildCMakeGeneratedProject(self):
        cmd = ["cmake", "--build", self._buildDir(), "--config", self._options.configuration]
        if self._options.makeargs:
            cmd.append("--")
            cmd.extend(self._options.makeargs)
        return self.execute(cmd)

    def _buildCMakeProject(self):
        self._generateBuildSystemFromCMakeProject()
        return self._buildCMakeGeneratedProject()
