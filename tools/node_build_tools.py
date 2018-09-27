import sys
import zipfile

if sys.platform.startswith('linux'):
    with zipfile.ZipFile('src/content/nw/tools/node_build_tools.zip', "r") as z:
        z.extractall('src/third_party/node-nw/build')
