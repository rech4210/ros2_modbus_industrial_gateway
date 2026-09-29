#!/bin/bash
set -e

# Source ROS 2 setup
source /opt/ros/jazzy/setup.bash

# Build workspace if install doesn't exist yet
if [ ! -f "/ros2_ws/install/setup.bash" ] && [ -d "/ros2_ws/src/ros2_modbus_gateway" ]; then
    echo "[gateway-entrypoint] Building workspace..."
    cd /ros2_ws
    colcon build --symlink-install
fi

# Source workspace install if built
if [ -f "/ros2_ws/install/setup.bash" ]; then
    source /ros2_ws/install/setup.bash
fi

# Set Fast DDS environment variables
if [ -f "/ros2_ws/src/ros2_modbus_gateway/config/fastdds.xml" ]; then
    export FASTRTPS_DEFAULT_PROFILES_FILE=/ros2_ws/src/ros2_modbus_gateway/config/fastdds.xml
fi
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export ROS_DOMAIN_ID=42

exec "$@"
