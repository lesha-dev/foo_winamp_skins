#include "stdafx.h"
#include "version.h"

DECLARE_COMPONENT_VERSION(COMPONENT_NAME, COMPONENT_VERSION,
    COMPONENT_NAME " " COMPONENT_VERSION "\n"
    COMPONENT_DESCRIPTION "\n"
    "Made by Alexey Komarov\n"
    COMPONENT_URL "\n\n"
    "Credits:\n"
    "- miniz by Rich Geldreich (MIT): .wsz archive reading\n"
    "- foobar2000 SDK by Peter Pawlowski\n"
    "Winamp is a trademark of its owners; its classic skin format and behaviour served as the reference. "
    "Skins are the property of their authors.\n\n"
    "Built " __DATE__ " " __TIME__ ".");

VALIDATE_COMPONENT_FILENAME(COMPONENT_FILENAME);
