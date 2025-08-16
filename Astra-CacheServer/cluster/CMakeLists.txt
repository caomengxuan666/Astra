file(GLOB_RECURSE CLUSTER_SOURCES *.cpp *.hpp *.h)
target_sources(Astra-CacheServer PRIVATE ${CLUSTER_SOURCES})