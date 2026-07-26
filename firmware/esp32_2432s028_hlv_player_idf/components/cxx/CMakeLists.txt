# ESP-IDF 5.5 adds its C++ runtime component to every component's common
# requirements, even when the application contains only C translation units.
# A project component has priority over the built-in component with the same
# name, so this empty interface component keeps the required dependency name
# while deliberately omitting libstdc++, exception stubs and C++ initializers.
idf_component_register()
