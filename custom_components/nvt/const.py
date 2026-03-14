from homeassistant.const import Platform

DOMAIN = "nvt"
PLATFORMS = (Platform.SENSOR, Platform.BINARY_SENSOR)

CONF_DIRECTION_NAME = "direction_name"
CONF_LINE_COLOR_BG = "line_color_bg"
CONF_LINE_COLOR_FG = "line_color_fg"
CONF_LINE_CODE = "line_code"
CONF_LINE_ID = "line_id"
CONF_LINE_NAME = "line_name"
CONF_LINE_TYPE = "line_type"
CONF_NETWORK = "network"
CONF_STOP_GROUP_KEY = "stop_group_key"
CONF_STOP_GROUP_NAME = "stop_group_name"
CONF_THRESHOLD_MINUTES = "threshold_minutes"

NETWORK_BDX = "bordeaux"
NETWORK_TLS = "toulouse"
NETWORK_LABELS = {
    NETWORK_BDX: "Bordeaux (TBM)",
    NETWORK_TLS: "Toulouse (Tisseo)",
}

DEFAULT_API_URL = "http://127.0.0.1:8080"
DEFAULT_NETWORK = NETWORK_BDX
DEFAULT_SCAN_INTERVAL = 30
DEFAULT_THRESHOLD_MINUTES = 5
DIRECTION_ALL = "__all__"
MAX_SCAN_INTERVAL = 300
MIN_SCAN_INTERVAL = 15
