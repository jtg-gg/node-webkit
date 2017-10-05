import sys, os
import subprocess

nw_exe = os.path.normpath(sys.argv[1])
nw_dll = os.path.normpath(sys.argv[2])
node_dll = os.path.normpath(sys.argv[3])
ffmpeg_dll = os.path.normpath(sys.argv[4])
libegl_dll = os.path.normpath(sys.argv[5])
libglesv2_dll = os.path.normpath(sys.argv[6])
swiftshader_libegl_dll = os.path.normpath(sys.argv[7])
swiftshader_libglesv2_dll = os.path.normpath(sys.argv[8])
nw_elf_dll = os.path.normpath(sys.argv[9])
dump_exe = os.path.normpath(sys.argv[10])
out_file = os.path.normpath(sys.argv[11])
sym_file = nw_exe + ".sym"
dll_sym_file = nw_dll + ".sym"
node_sym_file = node_dll + ".sym"
ffmpeg_sym_file = ffmpeg_dll + ".sym"
libegl_sym_file = libegl_dll + ".sym"
libglesv2_sym_file = libglesv2_dll + ".sym"
swiftshader_libegl_sym_file = swiftshader_libegl_dll + ".sym"
swiftshader_libglesv2_sym_file = swiftshader_libglesv2_dll + ".sym"
nw_elf_sym_file = nw_elf_dll + ".sym"
subprocess.call([dump_exe, nw_exe], stdout=open(sym_file, 'w'))
subprocess.call([dump_exe, nw_dll], stdout=open(dll_sym_file, 'w'))
subprocess.call([dump_exe, node_dll], stdout=open(node_sym_file, 'w'))
subprocess.call([dump_exe, ffmpeg_dll], stdout=open(ffmpeg_sym_file, 'w'))
subprocess.call([dump_exe, libegl_dll], stdout=open(libegl_sym_file, 'w'))
subprocess.call([dump_exe, libglesv2_dll], stdout=open(libglesv2_sym_file, 'w'))
subprocess.call([dump_exe, swiftshader_libegl_dll], stdout=open(swiftshader_libegl_sym_file, 'w'))
subprocess.call([dump_exe, swiftshader_libglesv2_dll], stdout=open(swiftshader_libglesv2_sym_file, 'w'))
subprocess.call([dump_exe, nw_elf_dll], stdout=open(nw_elf_sym_file, 'w'))
lzma_exe = os.path.join(os.path.dirname(os.path.realpath(__file__)), '..', '..', '..', 'third_party', 'lzma_sdk', 'Executable', '7za.exe')
subprocess.call([lzma_exe, 'a', '-tzip', '-mx9', out_file, sym_file, dll_sym_file, node_sym_file, ffmpeg_sym_file, libegl_sym_file, libglesv2_sym_file, swiftshader_libegl_sym_file, swiftshader_libglesv2_sym_file, nw_elf_sym_file])
