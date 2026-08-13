project('vscdef', ['c', 'cpp'],
  version: '1.0.0',
  default_options: ['c_std=c99', 'cpp_std=c++23', 'buildtype=release', 'b_ndebug=if-release', 'b_lto=true']
)
cc = meson.get_compiler('cpp').get_id()
if cc == 'msvc' or cc == 'gcc'
  warning('''vscdef only supports clang. It's the compiler that produces the fastest binary''')
endif
if cc == 'clang-cl' or cc == 'intel-cl' or cc == 'intel-llvm-cl'
  add_project_arguments('/clang:-ffinite-math-only', language : ['c', 'cpp'])
else
  add_project_arguments('-ffinite-math-only', language : ['c', 'cpp'])
endif
if cc == 'clang'
  add_project_link_arguments('-fuse-ld=lld', language: ['c', 'cpp'])
endif

sources = [
  'vscdefc/vscdef.cpp'
]

py = import('python').find_installation(pure: false)

vapoursynth_include = run_command(
  py, '-c', 'import vapoursynth as vs; print(vs.get_include())',
  check: true,
)
vapoursynth_include = include_directories(vapoursynth_include.stdout().strip())

shared_library('vscdef', sources,
  include_directories: [vapoursynth_include],
  install: true,
  install_dir: py.get_install_dir(pure: false, subdir: 'vapoursynth/plugins')
)

py.install_sources(
  'vscdef/__init__.py',
  'vscdef/vscdef.py',
  subdir: 'vscdef'
)
