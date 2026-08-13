project('vsletterbox', ['c', 'cpp'],
  version: '1.0.0',
  default_options: ['c_std=c99', 'cpp_std=c++23', 'buildtype=release', 'b_ndebug=if-release', 'b_lto=true']
)
cc = meson.get_compiler('cpp')
if cc.get_id() == 'msvc' or cc.get_id() == 'gcc'
  error('''vsletterbox only supports clang. It's the compiler that produces the fastest binary, and we've also utilised clang language extension to ensure faster speed''')
endif
if cc.get_id() == 'clang'
  add_project_arguments('-ffinite-math-only', language : ['c', 'cpp'])
elif cc.get_id() == 'clang-cl'
  add_project_arguments('/clang:-ffinite-math-only', language : ['c', 'cpp'])
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

shared_library('vsletterbox', sources,
  include_directories: [vapoursynth_include],
  install: true,
  install_dir: py.get_install_dir(pure: false, subdir: 'vapoursynth/plugins')
)

py.install_sources(
  'vscdef/__init__.py',
  'vscdef/vscdef.py',
  subdir: 'vscdef'
)
