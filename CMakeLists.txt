cmake_minimum_required(VERSION 3.21)

project(cuda_exact_nearest_neighbor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# S5.2 establishes infrastructure only. CPU/CUDA targets are added only after
# their design and implementation milestones are approved.
