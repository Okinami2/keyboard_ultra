#===============================================================================
# @brief TP78 Ultra keyboard
#===============================================================================

set(SOURCES_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/tp78_ultra_keyboard.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tp78_board.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tp78_keymap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tp78_matrix.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tp78_keyboard.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tp78_usb_keyboard.c
)

set(HEADER_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(SOURCES "${SOURCES}" ${SOURCES_LIST} PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" ${HEADER_LIST} PARENT_SCOPE)
