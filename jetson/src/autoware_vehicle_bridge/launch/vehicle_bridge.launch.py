"""Launch and activate the E-Trike lifecycle vehicle bridge."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, EmitEvent, RegisterEventHandler
from launch.event_handlers import OnProcessStart
from launch.events import matches_action
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import LifecycleNode
from launch_ros.event_handlers import OnStateTransition
from launch_ros.events.lifecycle import ChangeState
from launch_ros.substitutions import FindPackageShare
from lifecycle_msgs.msg import Transition


def generate_launch_description() -> LaunchDescription:
    can_interface = LaunchConfiguration("can_interface")
    engage_topic = LaunchConfiguration("engage_topic")
    parameter_file = PathJoinSubstitution(
        [FindPackageShare("autoware_vehicle_bridge"), "config", "etrike.param.yaml"]
    )

    bridge = LifecycleNode(
        package="autoware_vehicle_bridge",
        executable="vehicle_bridge_node",
        name="vehicle_bridge",
        output="screen",
        parameters=[parameter_file, {"can_interface": can_interface}],
        remappings=[("~/input/engage", engage_topic)],
    )

    configure = EmitEvent(
        event=ChangeState(
            lifecycle_node_matcher=matches_action(bridge),
            transition_id=Transition.TRANSITION_CONFIGURE,
        )
    )
    activate = EmitEvent(
        event=ChangeState(
            lifecycle_node_matcher=matches_action(bridge),
            transition_id=Transition.TRANSITION_ACTIVATE,
        )
    )

    return LaunchDescription(
        [
            DeclareLaunchArgument("can_interface", default_value="can0"),
            DeclareLaunchArgument(
                "engage_topic",
                default_value="/api/autoware/get/engage",
                description="Topic of type autoware_vehicle_msgs/msg/Engage the bridge "
                "subscribes to. Verify this is a PUBLISHED topic in the target Autoware "
                "Universe version; if engage is only exposed as an external-API service, "
                "this must be remapped to the real engage topic (e.g. /control/engage).",
            ),
            bridge,
            RegisterEventHandler(
                OnProcessStart(target_action=bridge, on_start=[configure])
            ),
            RegisterEventHandler(
                OnStateTransition(
                    target_lifecycle_node=bridge,
                    goal_state="inactive",
                    entities=[activate],
                )
            ),
        ]
    )
