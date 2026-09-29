import sys
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    port_arg = DeclareLaunchArgument(
        "port",
        default_value="8000",
        description="Port for HMI Web/WebSocket Server",
    )
    host_arg = DeclareLaunchArgument(
        "host",
        default_value="0.0.0.0",
        description="Host for HMI Web/WebSocket Server",
    )

    hmi_server = ExecuteProcess(
        cmd=[
            sys.executable,
            "-m",
            "hmi.run",
            "--host",
            LaunchConfiguration("host"),
            "--port",
            LaunchConfiguration("port"),
        ],
        output="screen",
    )

    return LaunchDescription([
        port_arg,
        host_arg,
        hmi_server,
    ])
