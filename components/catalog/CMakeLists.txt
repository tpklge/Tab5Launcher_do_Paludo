idf_component_register(SRCS "catalog.c" "catalog_sources.c" "catalog_model.c" "catalog_adapters.c" "catalog_cache.c" "catalog_transfer.c" "catalog_tracking.c" INCLUDE_DIRS "include"
 REQUIRES app_update storage esp_http_client mbedtls json board)
