idf_component_register(SRCS "main.c"
                             "control_task/control_task.c"
                             "comm_task/comm_task.c"
                             "queue_manager/queue_manager.c"
                            "json_protocol/json_protocol.c"
                             "keypad/keypad.c"
                    INCLUDE_DIRS "." "keypad")
