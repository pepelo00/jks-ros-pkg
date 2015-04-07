
import sys, os, threading, time, math, socket, random, subprocess
my_path = os.path.dirname(os.path.abspath(__file__))
sys.path.append(my_path + "/../blast_client")

import blast_network_bridge, blast_action_exec
import rospy

my_path = os.path.dirname(os.path.abspath(__file__))
sys.path.append(my_path + "/../blast_client")
exec_path = my_path + "/blast_action_exec.py"


class BlastROSError(Exception):
    __slots__ = ['value']
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return repr(self.value)

def install_action(robot_type, action_name):
    print "SPAM", robot_type, action_name
    return True

capabilities_dict = {"talker":
                         {"START": {"apt-pkgs": [], #list of apt-get packages
                                    "launch-files": [('rospy_tutorials', 'test-add-two-ints.launch'),], #(package-name, file) tuples
                                    "publishers": {"chatter": {"message": "std_msgs.String"},},
                                    "subscribers": {"chatter": {"message": "std_msgs.String"},},
                                    "services": {"/add_two_ints": {"message": "rospy_tutorials.AddTwoInts"},},
                                    },
                          "send-string": {"type": "pub", "topic": "chatter"},
                          "get-string": {"type": "sub-last", "topic": "chatter"},
                          "add": {"type": "service", "name": "/add_two_ints"},
                          },
                     }

class BlastRos():
    def __init__(self, node_name, capabilities):
        rospy.init_node(node_name)
        self.node_name = node_name
        self.capabilities = capabilities
        self.subscribers = {}
        self.last_data = {}
        self.data_lock = threading.Lock()
        self.publishers = {}
        self.msg_const = {}
        self.srv_const = {}
        self.srv_proxy = {}
        self.launch_processes = {}
        
        self.spin_thread_o = threading.Thread(target = self.spin_thread)
        self.spin_thread_o.start()

    def fill_message(self, msg, json):
        if type(json) == dict:
            for k, v in json.iteritems():
                if not hasattr(msg, k):
                    raise blast_action_exec.BlastRuntimeError("Invalid key for message: " + k)
                elif type(getattr(msg, k)) == str:
                    setattr(msg, k, str(v))
                elif type(getattr(msg, k)) in [long, float, bool, int]:
                    setattr(msg, k, v)
                else:
                    raise blast_action_exec.BlastRuntimeError("Invalid type for message: " + str(k) + " " + str(type(getattr(msg, k))))
        else:
            raise blast_action_exec.BlastRuntimeError("Invalid type for message: " + str(json) + " " + str(type(msg)))

    def ret_msg(self, msg):
        d = {}
        for k in msg.__slots__:
            v = getattr(msg, k)
            if type(v) in [str, long, float, int, bool] or v == None:
                d[k] = v
            else:
                raise BlastROSException("Invalid type for message: " + str(type(v)) + " from " + k)
        return d
        
    def ros_callback(self, msg, t):
        #print t, "->", msg
        rmsg = self.ret_msg(msg)
        self.data_lock.acquire()
        self.last_data[t] = rmsg
        self.data_lock.release()

    def spin_thread(self):
        rospy.spin()

    def capability_cb(self, cap, fn, param):
        if not cap in self.capabilities:
            print "Invalid capability", cap
            return None

        if fn == "START":
            print "Starting", cap

            for pkg, fil in self.capabilities[cap]["START"].get("launch-files", []):
                if (pkg, fil) not in self.launch_processes:
                    proc = subprocess.Popen(['roslaunch', pkg, fil])
                    self.launch_processes[(pkg, fil)] = proc
                    
        
            for topic, data in self.capabilities[cap]["START"].get("publishers", {}).iteritems():
                mtype = data["message"]
                mtype_folder = mtype.split(".")[0]
                exec("import " + mtype_folder + ".msg as rosmsg_" + mtype_folder)
                exec("self.publishers[topic] = rospy.Publisher(topic, rosmsg_" + mtype + ")")
                exec("self.msg_const[topic] = rosmsg_" + mtype)

            for topic, data in self.capabilities[cap]["START"].get("subscribers", {}).iteritems():
                mtype = data["message"]
                mtype_folder = mtype.split(".")[0]
                exec("import " + mtype_folder + ".msg as rosmsg_" + mtype_folder)
                exec("self.subscribers[topic] = rospy.Subscriber(topic, rosmsg_" + mtype 
                     + ", (lambda s: lambda x: s.ros_callback(x, '" + topic + "'))(self))")
                #exec("self.msg_const[topic] = rosmsg_" + mtype)
                
            for topic, data in self.capabilities[cap]["START"].get("services", {}).iteritems():
                mtype = data["message"]
                mtype_folder = mtype.split(".")[0]
                exec("import " + mtype_folder + ".srv as rossrv_" + mtype_folder)
                exec("self.srv_proxy[topic] = rospy.ServiceProxy(topic, rossrv_" + mtype + ")")
                exec("self.srv_const[topic] = (rossrv_" + mtype
                     + "Request, rossrv_" + mtype + "Response)")
                if data.get("wait", True):
                    rospy.wait_for_service(topic)
            return True
        
        if fn == "STOP":
            print "Stopping", cap
            w_queue = []
            for pkg, fil in self.capabilities[cap]["START"].get("launch-files", []):
                if (pkg, fil) in self.launch_processes:
                    w_queue.append(self.launch_processes[(pkg, fil)])
                    self.launch_processes[(pkg, fil)].terminate()
                    del self.launch_processes[(pkg, fil)]
            
            for topic, data in self.capabilities[cap]["START"].get("subscribers", {}).iteritems():
                del self.subscribers[topic]
                
            for topic, data in self.capabilities[cap]["START"].get("publishers", {}).iteritems():
                del self.publishers[topic]
                del self.msg_const[topic]

            for topic, data in self.capabilities[cap]["START"].get("services", {}).iteritems():
                del self.srv_proxy[topic]
                del self.srv_const[topic]
                
            for q in w_queue: q.wait()
            return True
    
        if not fn in self.capabilities[cap]:
            print "Invalid fn for", cap, "-", fn
            raise blast_action_exec.BlastRuntimeError("Invalid function: " + fn + " for capability " + cap)
            return None

        if self.capabilities[cap][fn]["type"] == "service":
            topic = self.capabilities[cap][fn]["name"]
            mtype = self.capabilities[cap]["START"]["services"][topic]["message"]
            msg = self.srv_const[topic][0]()
            self.fill_message(msg, param)
            resp = self.srv_proxy[topic](msg)
            return self.ret_msg(resp)
    
        if self.capabilities[cap][fn]["type"] == "pub":
            topic = self.capabilities[cap][fn]["topic"]
            mtype = self.capabilities[cap]["START"]["publishers"][topic]["message"]
            msg = self.msg_const[topic]()
            self.fill_message(msg, param)
            self.publishers[topic].publish(msg)
            #print "Sent", topic, msg
            return True
            
        if self.capabilities[cap][fn]["type"] == "sub-last":
            topic = self.capabilities[cap][fn]["topic"]
            mtype = self.capabilities[cap]["START"]["publishers"][topic]["message"]
            data = None
            while True:
                if rospy.is_shutdown():
                    raise BlastROSError("BLAST Node shutdown while ROS was stopping")
                self.data_lock.acquire()
                if topic in self.last_data:
                    data = self.last_data[topic]
                    self.data_lock.release()
                    break
                self.data_lock.release()
                time.sleep(0.001)
            return data

        raise blast_action_exec.BlastRuntimeError("Invalid function: " + fn + " for capability " + cap)
        return None



map_store = blast_network_bridge.MapStore(my_path + "/maps_client/")
action_store = blast_network_bridge.ActionStore(my_path + "/actions_client/")
blast_ros = BlastRos('blast_ros', capabilities_dict)
bnb = blast_network_bridge.BlastNetworkBridge("localhost", 8080, "stair4", "ros_tester",
                                              install_action, blast_ros.capability_cb, map_store, action_store)

bnb.start()
bnb.wait()
bnb.stop()
if bnb.error:
    print "The system terminated with error", bnb.error
else:
    print "Terminated without error"
