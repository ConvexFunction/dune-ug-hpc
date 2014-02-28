
add_executable("dune_ug_hpc" dune_ug_hpc.cc)
target_link_dune_default_libraries("dune_ug_hpc")

add_dune_ug_flags(dune_ug_hpc)
add_dune_mpi_flags(dune_ug_hpc)
