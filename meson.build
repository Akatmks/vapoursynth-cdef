project('vsletterbox', ['c', 'cpp'],
  version: '1.0.0',
  default_options: ['c_std=c99', 'cpp_std=c++23', 'buildtype=release', 'b_ndebug=if-release', 'b_lto=true']
)
add_project_arguments('-ffinite-math-only', language : ['c', 'cpp'])

sources = [
  'vscdefc/vscdef.cpp',
  'vscdefc/cdef_block.c'
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
  subdir: 'vscdef'
)
