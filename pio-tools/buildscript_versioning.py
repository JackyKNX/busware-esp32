FILENAME_BUILDNO = '.buildcounter'
FILENAME_VERSION = 'VERSION'
FILENAME_VERSION_H = 'include/version.h'

import datetime
import subprocess


def get_git_commit():
    try:
        return subprocess.check_output(
            ['git', 'rev-parse', '--short', 'HEAD'],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        return 'unknown'


version = '1.0'

try:
    with open(FILENAME_VERSION) as f:
        version = f.readline().strip()
except:
    print('Starting version number from 1.0')
    version = '1.0'
    with open(FILENAME_VERSION, 'w+') as f:
        f.write(version)

version = 'v' + version + '+'
build_no = 0

try:
    with open(FILENAME_BUILDNO) as f:
        build_no = int(f.readline()) + 1
except:
    print('Starting build number from 1..')
    build_no = 1

with open(FILENAME_BUILDNO, 'w+') as f:
    f.write(str(build_no))
    print('Build number: {}'.format(build_no))

build_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
git_commit = get_git_commit()

full_version = version + str(build_no)
version_short = full_version

print('Version: {}'.format(version_short))
print('Git commit: {}'.format(git_commit))
print('Build time: {}'.format(build_time))

hf = """
#ifndef BUILD_NUMBER
  #define BUILD_NUMBER "{}"
#endif

#ifndef VERSION
  #define VERSION "{} - {}"
#endif

#ifndef VERSION_SHORT
  #define VERSION_SHORT "{}"
#endif

#ifndef BUILD_TIME
  #define BUILD_TIME "{}"
#endif

#ifndef GIT_COMMIT
  #define GIT_COMMIT "{}"
#endif
""".format(
    build_no,
    full_version,
    build_time,
    version_short,
    build_time,
    git_commit
)

with open(FILENAME_VERSION_H, 'w+') as f:
    f.write(hf)