
import sys, json, traceback, math, os, socket, signal

TERMINATED_CC = False

def signal_handler(signal, frame):
    global TERMINATED_CC
    TERMINATED_CC = True
    sys.exit(0)
signal.signal(signal.SIGINT, signal_handler)


def enc_str(s):
    return s.replace("%", "%p").replace("\n", "%n").replace(",", "%c")
def dec_str(s):
    return s.replace("%n", "\n").replace("%c", ",").replace("%p", "%")

class BlastRuntimeError(Exception):
    __slots__ = ['value']
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return repr(self.value)

class BlastFindObjectError(Exception):
    __slots__ = ['value']
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return repr(self.value)


    
class BlastPos(object):
    def __init__(self, rx = None, ry = None, rz = None, x = None, y = None, z = None, surface = None, jsond = None, pos_string = None):
        self.rx, self.ry, self.rz = rx, ry, rz
        self.x, self.y, self.z = x, y, z
        self.surface = surface
        if jsond:
            self.rx, self.ry, self.rz = jsond['rx'], jsond['ry'], jsond['rz']
            self.x, self.y, self.z = jsond['x'], jsond['y'], jsond['z']
            self.surface = jsond.get("surface", self.surface)
        if pos_string:
            ar = [s.strip() for s in pos_string.strip().strip("Pos()").split(",")]
            self.x, self.y, self.z, self.rx, self.ry, self.rz = ar
        if self.rx: self.rx = float(self.rx)
        if self.ry: self.ry = float(self.ry)
        if self.rz: self.rz = float(self.rz)
        if self.x: self.x = float(self.x)
        if self.y: self.y = float(self.y)
        if self.z: self.z = float(self.z)
        
    def __str__(self):
        return ",".join([str(x) for x in [self.surface, self.x, self.y, self.z, self.rx, self.ry, self.rz]])
    def to_dict(self):
        return {'x': self.x, 'y': self.y, 'z': self.z,
                'rx': self.x, 'ry': self.ry, 'rz': self.rz}

class BlastObject(object):
    def __init__(self, uid = None, object_type = None, parent = None, pos = None, jsond = None):
        self.position = pos
        self.parent = parent
        self.uid = uid
        self.object_type = object_type
        if jsond:
            self.parent = str(jsond['parent'])
            self.position = BlastPos(jsond = jsond['position'], surface = self.parent)
            self.uid = int(jsond['uid'])
            self.object_type = str(jsond['object_type'])

class BlastLocation(object):
    __slots__ = ['_mid', '_x', '_y', '_a']
    def __init__(self, x = None, y = None, a = None, mid = None, jsond = None):
        self._x = x
        self._y = y
        self._a = a
        self._mid = mid
        if jsond:
            self._x = float(jsond['x'])
            self._y = float(jsond['y'])
            self._a = float(jsond['a'])
            self._mid = str(jsond['map'])
        if self._x: self._x = float(self._x)
        if self._y: self._y = float(self._y)
        if self._a: self._a = float(self._a)
        
        while self._a > +math.pi:
            self._a -= 2 * math.pi
        while self._a <= -math.pi:
            self._a += 2 * math.pi

        if self._mid == None:
            raise BlastRuntimeError("No map provided")

    @property
    def x(self): return self._x
    @property
    def y(self): return self._y
    @property
    def a(self): return self._a
    @property
    def mid(self): return self._mid

    def move(self, xp, yp = 0.0):
        return BlastLocation(x = self.x + xp * math.cos(self.a) - yp * math.sin(self.a),
                             y = self.y + xp * math.sin(self.a) + yp * math.cos(self.a),
                             a = self.a, mid = self.mid)
    def moveTo(self, xp, yp):
        return BlastLocation(x = xp, y = yp, a = self.a, mid = self.mid)
    def rotate(self, ad):
        return BlastLocation(x = self.x, y = self.y, a = self.a + ad, mid = self.mid)
    def rotateTo(self, ap):
        return BlastLocation(x = self.x, y = self.y, a = ap, mid = self.mid)

    def to_dict(self):
        return {'x': self.x, 'y': self.y, 'a': self.a, 'map': self.mid}

class BlastSurface(object):
    def __init__(self, name = None, state = None, surface_type = None, locations = None, jsond = None):
        self.name = name
        self.state = state
        self.surface_type = surface_type
        self.locations = locations
        if jsond:
            self.name = str(jsond["name"])
            self.state = str(jsond["state"])
            self.surface_type = str(jsond["surface_type"])
            self.locations = {}
            for name, loc in jsond["locations"].iteritems():
                self.locations[str(name)] = BlastLocation(jsond = loc)

#This is important so that no commands get sent
#over stdout, avoiding problems with mixing log
#data into IPC data. All log data goes into 
#stderr along with any generated error messages.
port = None
if __name__ == '__main__':
    try: 
        port = sys.argv[-1]
    except:
        port = None
    if port:
        ipc = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        ipc.connect(('127.0.0.1', int(port)))
        ipc_ind = ipc
    else:
        ipc = sys.stdout
        #ipc = os.fdopen(sys.stdout.fileno(), 'w', 0)
        ipc_ind = sys.stdin
        sys.stdout = sys.stderr
else:
    ipc = None
    ipc_ind = None


def ipc_write_packet(packet):
    ipc.send(packet + "\n")

ipc_buffer = ""
def ipc_packet(packet):
    global ipc_buffer
    #ipc.write(packet + BIG + "\n")
    #ipc.writeln(packet)
    #print packet
    ipc.send(packet + "\n")
    #sys.stdout.write(packet + "\n")
    #ipc.flush()
    #print packet
    #ipc.flush()
    #print "TO IPC", packet
    while ipc_buffer.find("\n") == -1:
        ipc_buffer += ipc_ind.recv(1024)
    r = ipc_buffer[:ipc_buffer.find("\n")]
    ipc_buffer = ipc_buffer[ipc_buffer.find("\n")+1:]
    #print "FROM IPC", r
    return r


def check_type(var, typ):
    if type(var) != typ:
        raise BlastRuntimeError("Invalid type for variable: " + str(var) + " type is " +
                                str(type(var)) + " and should be " + str(typ))

def check_world(world):
    pass

global_libs = {}
def add_library(robot, name, cl):
    global global_libs
    if not robot in global_libs:
        global_libs[robot] = {}
    global_libs[robot][name] = cl


class BlastActionLibrary():
    def __init__(self, caps):
        self.exc = None
        self.started = False
        self.capabilities = caps
    def _start(self):
        for c in self.capabilities:
            self.exc.capability(c, "START")
        self.started = True
    def _stop(self):
        if not self.started: return
        self.exc._libstop(self)
        for c in self.capabilities:
            self.exc.capability(c, "STOP")
        self.started = False
    def __del__(self):
        self._stop()
    #API
    def capability(self, c, s, p = None):
        if not self.started: raise Exception("Ran a library that has not been started")
        return self.exc.capability(c, s, p)
    

class BlastActionExec():
    def __init__(self):
        self.lib_obj = {}
        pass

    def _libstop(self, l):
        for rn in self.lib_obj:
            for ln in self.lib_obj[rn]:
                if self.lib_obj[rn][ln] == l:
                    del self.lib_obj[rn][ln]
    def get_library(self, rn, ln):
        global global_libs
        #TODO: if robot is None, current type
        if not rn in self.lib_obj:
            self.lib_obj[rn] = {}
        if ln in self.lib_obj[rn]:
            return self.lib_obj[rn][ln]
        if not rn in global_libs:
            raise Exception("Tried to load from a robot without a library: " + str(rn))
        if not ln in global_libs[rn]:
            raise Exception("Tried to load invalid library: " + str(rn) + " " + str(ln))
        li = global_libs[rn][ln]()
        li.exc = self
        li._start()
        self.lib_obj[rn][ln] = li
        return li

    def json(self, d):
        def r(a):
            if a == None: return None
            if type(a) == str or type(a) == bool or type(a) == int or type(a) == long or type(a) == float:
                return a
            if type(a) == unicode:
                return str(a)
            if type(a) == tuple or type(a) == set or type(a) == list:
                return [r(x) for x in a]
            if type(a) == dict:
                o = {}
                for k, v in a.iteritems():
                    o[r(k)] = r(v)
                return o
            if hasattr(a, "to_dict"):
                if callable(getattr(a, "to_dict")):
                    return r(a.to_dict())
            raise Exception("BLAST could not serialize: " + str(a))
        return json.dumps(r(d))

    #Executes a capability command. Returns none if it failed.
    def capability(self, cap, fn, param = None):
        res = ipc_packet("CAPABILITY," + cap + "," + fn + "," + enc_str(self.json(param)))
        r = dec_str(res)
        if r[0] == 'G':
            return json.loads(r[1:])
        raise Exception("Exception in capability " + cap + " " + fn + str(param) + " - " + r[1:])
        
    #Takes a name for a surface and gets the resulting JSON data structure.
    #Returns None if the surface does not exist.
    #TODO: describe the content of the structure.
    def get_surface(self, name, world=None):
        check_type(name, type(""))
        check_world(world)
        if world:
            res = ipc_packet("GET_SURFACE," + str(world) + "," + str(name) + "\n")
        else:
            res = ipc_packet("GET_SURFACE_NW," + str(name) + "\n")
        if res.find("SURFACE") == 0:
            return BlastSurface(jsond = json.loads(res[len("SURFACE"):]))
        return None

            
    
    #Takes a name for a object and gets the resulting BlastObject
    #Returns None if the object does not exist.
    def get_object(self, uid, world=None):
        check_type(uid, type(0))
        check_world(world)
        if world:
            res = ipc_packet("GET_OBJECT," + str(world) + "," + str(uid) + "\n")
        else:
            res = ipc_packet("GET_OBJECT_NW," + str(uid) + "\n")
        if res.find("OBJECT") == 0:
            return BlastObject(jsond = json.loads(res[len("OBJECT"):]))
        return None

    #Gets the UID of the object in a holder of the robot. Takes
    #the name of the holder. Returns none if no object or invalid
    #holder, or the UID as an int.
    def get_robot_holder(self, holder, world=None):
        check_type(holder, type(""))
        check_world(world)
        if world:
            res = ipc_packet("GET_ROBOT_HOLDER," + str(world) + "," + str(holder) + "\n").strip()
        else:
            res = ipc_packet("GET_ROBOT_HOLDER_NW," + str(holder) + "\n").strip()
        if res == "None":
            res = None
        else:
            try:
                res = int(res)
            except:
                pass
        return res
    
    #Set the failure state of the action. Takes the failure mode
    #of the action. Throws a BlastRuntimeError if the failure mode
    #is invalid or another problem occurs.
    def set_failure(self, mode):
        check_type(mode, type(""))
        res = ipc_packet("SET_FAILURE," + str(mode) + "\n").strip()
        if res == "None": res = None
        if res == "True": res = True
        if res == "False": res = False
        if res == None or res == False:
            raise BlastRuntimeError("Failed to set failure mode")
        return res

    #Deletes an object from a surface in the world. Takes the UID of the
    #object as an int or string. Throws a BlastRuntimeError if the object
    #cannot be deleted or does not exist in the first place. To delete
    #objects that are in the robot's holders, use set_robot_holder with None.
    def delete_surface_object(self, obj, world=None):
        check_type(obj, type(0))
        check_world(world)
        if world:
            res = ipc_packet("DELETE_SURFACE_OBJECT," + str(world) + "," + str(obj) + "\n").strip()
        else:
            res = ipc_packet("DELETE_SURFACE_OBJECT_NW," + str(obj) + "\n").strip()
        if res == "None": res = None
        if res == "True": res = True
        if res == "False": res = False
        if res == None or res == False:
            raise BlastRuntimeError("Failed to delete object")
        return res
    
    #Executes an action immediately. It takes the name of the action as a string,
    #the parameters for the action as a dictionary (of strings, and ints). The
    #action takes place in the context of the current action, so the parent action
    #is responsible for the time taken by the child. This is generally used as a
    #tool to execute e.g. the move to arbitrary location actions as a component of
    #the global, more complex action.
    def take_action(self, action, parameters, world=None):
        check_type(action, type(""))
        check_type(parameters, type({}))
        #if world_limits != None: check_type(world_limits, type({}))
        check_world(world)

        parameters = parameters.copy()
        for name, value in parameters.iteritems():
            if type(value) == BlastPos or type(value) == BlastLocation:
                parameters[name] = value.to_dict()


        #if world_limits != None:
        #    world_limits = world_limits.copy()
        #    for name, value in world_limits.iteritems():
        #        if type(value) == BlastPos or type(value) == BlastLocation:
        #            world_limits[name] = value.to_dict()
        #    print world_limits
        #    parameters = {"parameter values": parameters, "world limits": world_limits}
        if world:
            res = ipc_packet("TAKE_ACTION," + str(world) + "," + str(action) + "," + self.json(parameters) + "\n")
        else:
            res = ipc_packet("TAKE_ACTION_NW," + str(action) + "," + self.json(parameters) + "\n")
        if type(res) == type(""):
            if res.strip() == "None": res = None
        if res == None:
            raise BlastRuntimeError("Failed to take action")
        return res

    #Plans a hunt for an object of a given type. This causes the robot to iterate
    #through various plans attempting to find an object of a given type. Since all
    #objects of a given type are considered equivalent, it will grab the first one
    #it can. The function takes the name of the holder to place the object in and
    #the string name of the object type to find. It returns the uid, position, and
    #prior surface of the object. It raises a BlastRuntimeError if planning fails
    #in a matter that suggests an error with the system, and a BlastFindObjectError
    #if the object cannot actually be found.
    #TODO remove
    def plan_hunt(self, holder, object_type, world=None):
        check_type(holder, type(""))
        check_type(object_type, type(""))
        check_world(world)
        if world:
            res = ipc_packet("PLAN_HUNT," + str(world) + "," + str(holder) + "," + str(object_type) + "\n")
        else:
            res = ipc_packet("PLAN_HUNT_NW," + str(holder) + "," + str(object_type) + "\n")
        if res.strip() == "None": res = None
        elif res.strip() == "False": res = False
        if res == None:
            raise BlastRuntimeError("Failed to plan")
        if res == False:
            raise BlastFindObjectError("Failed to find " + object_type + " and place it in " + holder)
        strs = [x.strip().replace("Pos", "").strip("()").strip() for x in res.strip().split(",")]
        return int(strs[0]), BlastPos(x=strs[1], y=strs[2], z=strs[3], 
                                      rx=strs[4], ry=strs[5], rz=strs[6], surface=strs[-1]), strs[-1]

    #Add an object of a given type and position to the surface. Takes the 
    #name of the surface, the name of the object_type and the position.
    #Raises a BlastRuntimeError if it fails.
    def surface_add_object(self, surface, object_type, pos, world = None):
        check_type(surface, type(""))
        check_type(object_type, type(""))
        check_type(pos, BlastPos)
        check_world(world)
        
        pos = ",".join(str(pos).split(",")[1:]) #Get rid of the surface here.
        if world:
            res = ipc_packet("ADD_SURFACE_OBJECT," + str(world) + "," + str(surface) + "," + str(object_type) + "," + pos + "\n").strip()
        else:
            res = ipc_packet("ADD_SURFACE_OBJECT_NW," + str(surface) + "," + str(object_type) + "," + pos + "\n").strip()
        if res == "None": res = None
        if res == "True": res = True
        if res == "False": res = False
        if res == None or res == False:
            raise BlastRuntimeError("Failed to add surface object")
        return res
        
    #Lists all uids of all the objects on a given surface. Takes the
    #surface and returns the list of UIDs as ints. Raises a
    #BlastRuntimeError if it fails.
    def list_surface_objects(self, surface, world = None):
        check_type(surface, type(""))
        check_world(world)
        if world:
            res = ipc_packet("LIST_SURFACE_OBJECTS," + str(world) + "," + str(surface) + "\n").strip()
        else:
            res = ipc_packet("LIST_SURFACE_OBJECTS_NW," + str(surface) + "\n").strip()
        if res == "None": res = None
        if res == "True": res = True
        if res == "False": res = False
        if res == None or res == False:
            raise BlastRuntimeError("Failed to list surface objects")
        return [int(x.strip()) for x in res.split(",")]

    #Flags to the system that a surface has been scanned for objects.
    #Takes the name of the surface, and the list of object types as
    #a list of strings or a comma-seperated string. Raises a
    #BlastRuntimeError if it fails for any reason.
    def surface_scan(self, surface, object_types, world=None):
        check_type(surface, type(""))
        check_world(world)
        if type(object_types) == type([]) or type(object_types) == type((1,2)):
            object_types = ",".join([str(x) for x in object_types])
        check_type(object_types, type(""))
        if world:
            res = ipc_packet("SURFACE_SCAN," + str(world) + "," + str(surface) + "," + str(object_types) + "\n")
        else:
            res = ipc_packet("SURFACE_SCAN_NW," + str(surface) + "," + str(object_types) + "\n")
        if res.strip() == "None": res = None
        if res == "True": res = True
        if res == "False": res = False
        if res == None or res == False:
            raise BlastRuntimeError("Failed to set surface scaned")
        return res
        
    #Plans to set down an object on a surface. Takes the UID of the object,
    #the name of the surface, and the position on the surface. Raises
    #a BlastRuntimeError if it fails.
    #TODO remove
    def plan_place(self, uid, surface, pos, world=None):
        check_type(uid, type(0))
        check_type(surface, type(""))
        check_type(pos, BlastPos)
        check_world(world)
        if world:
            res = ipc_packet("PLAN_PLACE," + str(world) + "," + str(uid) + "," + str(surface) + "," + str(pos))
        else:
            res = ipc_packet("PLAN_PLACE_NW," + str(uid) + "," + str(surface) + "," + str(pos))
        if res: res = res.strip().strip("()").strip()
        if res == "None": res = None
        elif res == "True": res = True
        elif res == "False": res = False
        if res == False or res == None:
            raise BlastRuntimeError("Failed to plan")
        return res
        
    #Sets the position of one of the robot's joints. Be careful with this function as
    #it can cause BLAST to declare that your application has failed when it hasn't.
    #Takes the name of the position to set and the list of joint values. Raises a
    #BlastRuntimeError if it fails.
    def set_robot_position(self, pos, val, world=None):
        check_type(pos, type(""))
        #TODO: check val type
        check_world(world)
        if world:
            res = ipc_packet("SET_ROBOT_POSITION," + str(world) + "," + str(pos) + "," + self.json(val) + "\n")
        else:
            res = ipc_packet("SET_ROBOT_POSITION_NW," + str(pos) + "," + self.json(val) + "\n")
        if res == "None": res = None
        if res == "True": res = True
        if res == "False": res = False
        if res == None or res == False:
            raise BlastRuntimeError("Failed to set robot position")
        return res

    #Sets what object is in the holder of the robot. Takes the holder, and the object type.
    #If require_preexisting_object is set to True, then an object must be in the holder.
    #Note that this function replaces the object, changing its UID. If it fails, it raises
    #a BlastRuntimeError. #TODO: make function return object UID.
    def set_robot_holder(self, holder, ot, require_preexisting_object = True, world = None):
        check_type(holder, type(""))
        if ot != None: check_type(ot, type(""))
        check_type(require_preexisting_object, type(False))
        check_world(world)
        if ot == None: ot = "None()"
        if world:
            res = ipc_packet("SET_ROBOT_HOLDER," + str(world) + "," + str(holder) + "," 
                             + str(ot) + "," + str(require_preexisting_object) + "\n")
        else:
            res = ipc_packet("SET_ROBOT_HOLDER_NW," + str(holder) + "," 
                             + str(ot) + "," + str(require_preexisting_object) + "\n")
        if res == "None": res = None
        if res == "True": res = True
        if res == "False": res = False
        if res == None or res == False:
            raise BlastRuntimeError("Failed to set robot holder")
        return res

    #Signals Transfer an object from one robot holder to another. Takes the name 
    #of the first holder and the second holder, preserves object UID. Raises a
    #BlastRuntimeError if it fails for any reason, including an empty start.
    def robot_transfer_holder(self, from_holder, to_holder, world = None):
        check_type(from_holder, type(""))
        check_type(to_holder, type(""))
        check_world(world)
        if world:
            res = ipc_packet("ROBOT_TRANSFER_HOLDER," + str(world) + "," 
                             + str(from_holder) + "," + str(to_holder) + "\n")
        else:
            res = ipc_packet("ROBOT_TRANSFER_HOLDER_NW," 
                             + str(from_holder) + "," + str(to_holder) + "\n")
        if res == "None": res = None
        if res == "True": res = True
        if res == "False": res = False
        if res == None or res == False:
            raise BlastRuntimeError("Failed to transfer robot holder")
        return res

    #Signals transfer of an object from a surface and places it into the
    #holder. The function takes the uid and the holder name and raises a
    #BlastRuntimeError if it fails for any reason.
    def robot_pick_object(self, objectref, to_holder, world = None):
        check_type(objectref, type(0))
        check_type(to_holder, type(""))
        check_world(world)
        if world:
            res = ipc_packet("ROBOT_PICK_OBJECT," + str(world) + "," 
                             + str(objectref) + "," + str(to_holder) + "\n")
        else:
            res = ipc_packet("ROBOT_PICK_OBJECT_NW," 
                             + str(objectref) + "," + str(to_holder) + "\n")
        if res == "None": res = None
        if res == "True": res = True
        if res == "False": res = False
        if res == None or res == False:
            raise BlastRuntimeError("Failed to pick object")
        return res
        
    #Signals the transfer of an object from the robots holder to the
    #surface. This takes the holder and the position of the object (which
    #includes the surface). Raises a BlastRuntimeError if it fails.
    def robot_place_object(self, holder, position, world = None):
        check_type(holder, type(""))
        check_type(position, BlastPos)
        check_world(world)
        #if type(position) == type([0, 1]) or position == type((1, 2)):
        #    position = ",".join([str(x) for x in position])
        #ps = ",".join([str(x).strip() for x in position.strip().strip('[]()').strip().replace("Pos(", "").replace(")", "").split(",")])

        #print position, holder

        if world:
            res = ipc_packet("ROBOT_PLACE_OBJECT," + str(world) + "," 
                             + str(holder) + "," + str(position) + "\n")
        else:
            res = ipc_packet("ROBOT_PLACE_OBJECT_NW," 
                             + str(holder) + "," + str(position) + "\n")
        if res == "None": res = None
        if res == "True": res = True
        if res == "False": res = False
        if res == None or res == False:
            raise BlastRuntimeError("Failed to place object")
        return res

    #Sets the location of the robot in the world. Takes the position and
    #raises a BlastRuntimeError on failure.
    def set_location(self, position, world = None):
        check_type(position, BlastLocation)
        check_world(world)
        if world:
            res = ipc_packet("SET_ROBOT_LOCATION," + str(world) + "," 
                             + self.json(position.to_dict()) + "\n")
        else:
            res = ipc_packet("SET_ROBOT_LOCATION_NW," + self.json(position.to_dict()) + "\n")
        if res == "None": res = None
        if res == "True": res = True
        if res == "False": res = False
        if res == None or res == False:
            raise BlastRuntimeError("Failed to set location of object")
        return res

    def get_teleop(self):
        check_world(None)
        res = ipc_packet("GET_TELEOP\n")
        if res == "None": res = None
        if res == "True": res = True
        if res == "False": res = False
        if res == None or res == False or res == True:
            return res
        if res.find("TELEOP") != 0:
            raise BlastRuntimeError("Invalid return for teleop")
        return json.loads(res[len("TELEOP"):])
    
    #Gets the location of the robot in the world.
    def get_location(self, world = None):
        check_world(world)
        if world:
            res = ipc_packet("GET_ROBOT_LOCATION," + str(world) + "\n")
        else:
            res = ipc_packet("GET_ROBOT_LOCATION_NW\n")
        if res == "None": res = None
        if res == "False": res = False
        if res.find("LOCATION") == 0:
            res = BlastLocation(jsond = json.loads(res[len("LOCATION"):]))
        if res == None or res == False:
            raise BlastRuntimeError("Failed to set location of object")
        return res
    
    def run(self):
        raise Exception("Need to override run method")

def set_action_exec(ex):
    ex().run(parameters) #Nones for legacy

if __name__ == '__main__':
    try:
        parameters_c = json.loads(sys.argv[2])
        parameters = {}
        for name, value in parameters_c.iteritems():
            if type(value) == type(1) or type(value) == type(1.0) or type(value) == type(1L):
                parameters[str(name)] = value
            elif type(value) == type({}):
                if "x" in value and "y" in value and "a" in value and "map" in value:
                    parameters[str(name)] = BlastLocation(jsond = value)
                else:
                    raise Exception("Invalid parameter type for " + str(value))
            elif type(value) == type([]):
            #Is a position
                if len(value) == 2:
                    if type(value[1]) == dict:
                        parameters[str(name)] = BlastPos(surface = value[0], jsond = value[1])
                    else:
                        parameters[str(name)] = BlastPos(surface = value[0], pos_string = value[1])
                else:
                    raise Exception("Invalid parameter type for " + str(value))
            elif type(value) == type("") or type(value) == type(u""):
                parameters[str(name)] = str(value)
            else:
                raise Exception("Invalid parameter type for " + str(value))
        for libf in sys.argv[3:-1]:
            execfile(libf)
        execfile(sys.argv[1])
        if TERMINATED_CC:
            ipc_write_packet("TERMINATE_ALL\n")
        else:
            ipc_write_packet("TERMINATE\n")
    except:
        print "Exception in action:", sys.argv
        print '-'*60
        traceback.print_exc(file=sys.stdout)
        print '-'*60
        try:
            ipc_write_packet("ERROR\n")
        except:
            print "Failed to write error"

#else:
#    print "Not running because action is not main."
















