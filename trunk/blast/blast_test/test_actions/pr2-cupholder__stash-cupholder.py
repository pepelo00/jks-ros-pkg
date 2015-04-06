
class BlastPr2StashCupHolderActionExec(BlastActionExec):
    def __init__(self):
        BlastActionExec.__init__(self)
    def run(self, parameters):
        self.capability("driver", "START")
        self.capability("driver", "WAIT_LEFT_ARM", [0.0, -0.350, 0.0, -1.225, 3.14159, -1.65, 0.0, False])
        self.robot_transfer_holder("left-arm", "cupholder")
set_action_exec(BlastPr2StashCupHolderActionExec)