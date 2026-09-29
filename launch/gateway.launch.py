import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory("ros2_modbus_gateway")
    default_config_path = os.path.join(pkg_share, "config", "gateway.yaml")

    config_arg = DeclareLaunchArgument(
        "config_file",
        default_value=default_config_path,
        description="Path to the gateway parameters YAML file",
    )

    gateway_node = Node(
        package="ros2_modbus_gateway",
        executable="gateway_node",
        name="gateway_node",
        output="screen",
        parameters=[LaunchConfiguration("config_file")],
    )

    return LaunchDescription([
        config_arg,
        gateway_node,
    ])
