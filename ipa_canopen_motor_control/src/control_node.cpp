#include <ipa_can_interface/dispatcher.h>
#include <ipa_can_interface/socketcan.h>
#include <ipa_canopen_chain_ros/chain_ros.h>

#include <ipa_canopen_402/ipa_canopen_402.h>

#include <hardware_interface/joint_command_interface.h>
#include <hardware_interface/joint_state_interface.h>
#include <hardware_interface/robot_hw.h>

#include <joint_limits_interface/joint_limits.h>
#include <joint_limits_interface/joint_limits_urdf.h>
#include <joint_limits_interface/joint_limits_rosparam.h>
#include <joint_limits_interface/joint_limits_interface.h>
#include <urdf/model.h>

#include <controller_manager/controller_manager.h>

using namespace ipa_can;
using namespace ipa_canopen;


//typedef Node_402 MotorNode;

class MotorNode : public Node_402{
    double scale_factor;
public:
   MotorNode(boost::shared_ptr <ipa_canopen::Node> n, const std::string &name) : Node_402(n, name), scale_factor(360*1000/(2*M_PI)) {
   }
   const double getActualPos() { return Node_402::getActualPos() / scale_factor; }
   const double getActualVel() { return Node_402::getActualVel() / scale_factor; }
   const double getActualEff() { return Node_402::getActualEff() / scale_factor; }
   void setTargetPos(const double &v) { Node_402::setTargetPos(v*scale_factor); }
   void setTargetVel(const double &v) { Node_402::setTargetVel(v*scale_factor); }
   void setTargetEff(const double &v) { Node_402::setTargetEff(v*scale_factor); }
   const double getTargetPos() { return Node_402::getTargetPos() / scale_factor; }
   const double getTargetVel() { return Node_402::getTargetVel() / scale_factor; }
   const double getTargetEff() { return Node_402::getTargetEff() / scale_factor; }
};

class HandleLayer: public SimpleLayer{
    boost::shared_ptr<MotorNode> motor_;
    double pos_, vel_, eff_;
    double cmd_pos_, cmd_vel_, cmd_eff_;

    
    hardware_interface::JointStateHandle jsh_;
    hardware_interface::JointHandle jph_, jvh_, jeh_, *jh_;
    
    typedef boost::unordered_map< MotorNode::OperationMode,hardware_interface::JointHandle* > CommandMap;
    CommandMap commands_;

    template <typename T> hardware_interface::JointHandle* addHandle( T &iface, hardware_interface::JointHandle *jh,  const std::vector<MotorNode::OperationMode> & modes){

        uint32_t mode_mask = 0;
        for(size_t i=0; i < modes.size(); ++i){
            if(motor_->isModeSupported(modes[i]))
                mode_mask |= MotorNode::getModeMask(modes[i]);
        }
        if(mode_mask == 0) return 0;

        iface.registerHandle(*jh);
        
        for(size_t i=0; i < modes.size(); ++i){
            commands_[modes[i]] = jh;
        }
        return jh;
    }
    bool select(const MotorNode::OperationMode &m){
        CommandMap::iterator it = commands_.find(m);
        if(it == commands_.end()) return false;
        jh_ = it->second;
        return true;
    }
public:
    HandleLayer(const std::string &name, const boost::shared_ptr<MotorNode> & motor)
    : SimpleLayer(name + " Handle"), motor_(motor), jsh_(name, &pos_, &vel_, &eff_), jph_(jsh_, &cmd_pos_), jvh_(jsh_, &cmd_vel_), jeh_(jsh_, &cmd_eff_), jh_(0) {}

    int canSwitch(const MotorNode::OperationMode &m){
       if(motor_->getMode() == m) return -1;
       if(commands_.find(m) != commands_.end()) return 1;
       return 0;
    }
    bool switchMode(const MotorNode::OperationMode &m){
        CommandMap::iterator it = commands_.find(m);
        if(it == commands_.end()) return false;

        return motor_->enterModeAndWait(m) && select(m);
    }

    void registerHandle(hardware_interface::JointStateInterface &iface){
        iface.registerHandle(jsh_);
    }
    hardware_interface::JointHandle* registerHandle(hardware_interface::PositionJointInterface &iface){
        std::vector<MotorNode::OperationMode> modes;
        modes.push_back(MotorNode::Profiled_Position);
        modes.push_back(MotorNode::Interpolated_Position);
        modes.push_back(MotorNode::Cyclic_Synchronous_Position);
        return addHandle(iface, &jph_, modes);
    }
    hardware_interface::JointHandle* registerHandle(hardware_interface::VelocityJointInterface &iface){
        std::vector<MotorNode::OperationMode> modes;
        modes.push_back(MotorNode::Velocity);
        modes.push_back(MotorNode::Profiled_Velocity);
        modes.push_back(MotorNode::Cyclic_Synchronous_Velocity);
        return addHandle(iface, &jvh_, modes);
    }
    hardware_interface::JointHandle* registerHandle(hardware_interface::EffortJointInterface &iface){
        std::vector<MotorNode::OperationMode> modes;
        modes.push_back(MotorNode::Profiled_Torque);
        modes.push_back(MotorNode::Cyclic_Synchronous_Torque);
        return addHandle(iface, &jeh_, modes);
    }
    virtual bool read() {
        bool okay = true;
        // okay = motor.okay();
        if(okay){
            cmd_pos_ = pos_ = motor_->getActualPos();
            cmd_vel_ = vel_ = motor_->getActualVel();
            cmd_eff_ = eff_ = motor_->getActualEff();
            if(!jh_){
                MotorNode::OperationMode m = motor_->getMode();
                if(m != MotorNode::No_Mode) return select(m);
            }
            if(jh_ == &jph_){
                cmd_pos_ = motor_->getTargetPos();
            }else if(jh_ == &jvh_){
                cmd_vel_ = motor_->getTargetVel();
            }else if(jh_ == &jeh_){
                cmd_eff_ = motor_->getTargetEff();
            }
        }
        return okay;
    }
    virtual bool write() {
        if(jh_){
            if(jh_ == &jph_){
                motor_->setTargetPos(cmd_pos_);
            }else if(jh_ == &jvh_){
                motor_->setTargetVel(cmd_vel_);
            }else if(jh_ == &jeh_){
                motor_->setTargetEff(cmd_eff_);
            }
            return true;
        }
        return motor_->getMode() == MotorNode::No_Mode;
    }
    virtual bool report() { return true; }
    virtual bool init() {
        select(motor_->getMode());
        return true;
    }
    virtual bool recover() {
        return true;
    }
    virtual bool shutdown(){
        return true;
    }
};

class ControllerManagerLayer : public SimpleLayer, public hardware_interface::RobotHW {
    ros::NodeHandle nh_;
    boost::shared_ptr<controller_manager::ControllerManager> cm_;
    boost::mutex mutex_;
    bool recover_;
    bool paused_;
    ros::Time last_time_;
    ControllerManagerLayer * this_non_const;
    
    void update(){
        ros::Time now = ros::Time::now();
        ros::Duration period(now -last_time_);
        cm_->update(now, period, recover_);
        recover_ = false;
        last_time_ = now;

        pos_saturation_interface_.enforceLimits(period);
        pos_soft_limits_interface_.enforceLimits(period);
        vel_saturation_interface_.enforceLimits(period);
        vel_soft_limits_interface_.enforceLimits(period);
        eff_saturation_interface_.enforceLimits(period);
        eff_soft_limits_interface_.enforceLimits(period);
    }

    hardware_interface::JointStateInterface state_interface_;
    hardware_interface::PositionJointInterface pos_interface_;
    hardware_interface::VelocityJointInterface vel_interface_;
    hardware_interface::EffortJointInterface eff_interface_;

    joint_limits_interface::PositionJointSoftLimitsInterface pos_soft_limits_interface_;
    joint_limits_interface::PositionJointSaturationInterface pos_saturation_interface_;
    joint_limits_interface::VelocityJointSoftLimitsInterface vel_soft_limits_interface_;
    joint_limits_interface::VelocityJointSaturationInterface vel_saturation_interface_;
    joint_limits_interface::EffortJointSoftLimitsInterface eff_soft_limits_interface_;
    joint_limits_interface::EffortJointSaturationInterface eff_saturation_interface_;
    
    typedef boost::unordered_map< std::string, boost::shared_ptr<HandleLayer> > HandleMap;
    HandleMap handles_;

    void pause(){
        boost::mutex::scoped_lock lock(mutex_);
        if(cm_) paused_ = true;
    }
    void resume(){
        boost::mutex::scoped_lock lock(mutex_);
        if(paused_){
            paused_ = false;
            recover_ = true;
        }
    }
    
public:
    virtual bool checkForConflict(const std::list<hardware_interface::ControllerInfo>& info) const{
        bool in_conflict = RobotHW::checkForConflict(info);
        if(in_conflict) return true;

        typedef std::vector<std::pair <boost::shared_ptr<HandleLayer>, MotorNode::OperationMode> >  SwitchContainer;
        SwitchContainer to_switch;
        to_switch.reserve(handles_.size());

        for (std::list<hardware_interface::ControllerInfo>::const_iterator info_it = info.begin(); info_it != info.end(); ++info_it){
            ros::NodeHandle nh(nh_,info_it->name);
            int mode;
            if(!nh.getParam("required_drive_mode", mode)) continue;

            for (std::set<std::string>::const_iterator res_it = info_it->resources.begin(); res_it != info_it->resources.end(); ++res_it){
                boost::unordered_map< std::string, boost::shared_ptr<HandleLayer> >::const_iterator h_it = handles_.find(*res_it);

                if(h_it == handles_.end()){
                    ROS_ERROR_STREAM(*res_it << " not found");
                    return true;
                }
                if(int res = h_it->second->canSwitch((MotorNode::OperationMode)mode)){
                    if(res > 0) to_switch.push_back(std::make_pair(h_it->second, MotorNode::OperationMode(mode)));
                }else{
                    ROS_ERROR_STREAM("Mode " << mode << " is not available for " << *res_it);
                    return true;
                }
            }
        }

        if(!to_switch.empty()){
            this_non_const->pause();
            for(SwitchContainer::iterator it = to_switch.begin(); it != to_switch.end(); ++it){
                // TODO: rollback
                if(!it->first->switchMode(it->second)) return true;
            }
            
            ///call enforceLimits with large period in order to reset their internal prev_cmd_ value!
            ros::Duration period(1000000000.0);
            this_non_const->pos_saturation_interface_.enforceLimits(period);
            this_non_const->pos_soft_limits_interface_.enforceLimits(period);
            this_non_const->vel_saturation_interface_.enforceLimits(period);
            this_non_const->vel_soft_limits_interface_.enforceLimits(period);
            this_non_const->eff_saturation_interface_.enforceLimits(period);
            this_non_const->eff_soft_limits_interface_.enforceLimits(period);

            /*try{  ej_sat_interface_.enforceLimits(period);  }
            catch(const joint_limits_interface::JointLimitsInterfaceException&){}
            try{  ej_limits_interface_.enforceLimits(period);  }
            catch(const joint_limits_interface::JointLimitsInterfaceException&){}
            try{  pj_sat_interface_.enforceLimits(period);  }
            catch(const joint_limits_interface::JointLimitsInterfaceException&){}
            try{  pj_limits_interface_.enforceLimits(period);  }
            catch(const joint_limits_interface::JointLimitsInterfaceException&){}
            try{  vj_sat_interface_.enforceLimits(period);  }
            catch(const joint_limits_interface::JointLimitsInterfaceException&){}
            try{  vj_limits_interface_.enforceLimits(period);  }
            catch(const joint_limits_interface::JointLimitsInterfaceException&){}
            */

            this_non_const->resume();
        }

        return false;
    }

    ControllerManagerLayer(const ros::NodeHandle &nh)
    :SimpleLayer("ControllerManager"), nh_(nh), recover_(false), paused_(false), last_time_(ros::Time::now()), this_non_const(this) {
        registerInterface(&state_interface_);
        registerInterface(&pos_interface_);
        registerInterface(&vel_interface_);
        registerInterface(&eff_interface_);

        registerInterface(&pos_saturation_interface_);
        registerInterface(&pos_soft_limits_interface_);
        registerInterface(&vel_saturation_interface_);
        registerInterface(&vel_soft_limits_interface_);
        registerInterface(&eff_saturation_interface_);
        registerInterface(&eff_soft_limits_interface_);
        
    }

    virtual bool read() {
        boost::mutex::scoped_lock lock(mutex_);
        return cm_;
    }
    virtual bool write()  {
        boost::mutex::scoped_lock lock(mutex_);
        if(cm_ && !paused_){
            update();
        }
        return cm_;
    }
    virtual bool report() { return true; }
    bool register_handles(){
        urdf::Model urdf;
        urdf.initParam("robot_description");

        for(HandleMap::iterator it = handles_.begin(); it != handles_.end(); ++it){
            joint_limits_interface::JointLimits limits;
            joint_limits_interface::SoftJointLimits soft_limits;

            boost::shared_ptr<const urdf::Joint> joint = urdf.getJoint(it->first);
            if(!joint){
                return false;
            }

            bool has_joint_limits = joint_limits_interface::getJointLimits(joint, limits);

            has_joint_limits = joint_limits_interface::getJointLimits(it->first, nh_, limits) || has_joint_limits;

            bool has_soft_limits = has_joint_limits && joint_limits_interface::getSoftJointLimits(joint, soft_limits);

            if(!has_joint_limits){
                ROS_WARN_STREAM("No limits found for " << it->first);
            }

            it->second->registerHandle(state_interface_);

            const hardware_interface::JointHandle *h  = 0;
            h = it->second->registerHandle(pos_interface_);
            if(h && has_joint_limits){
                joint_limits_interface::PositionJointSaturationHandle sathandle(*h, limits);
                pos_saturation_interface_.registerHandle(sathandle);
                if(has_soft_limits){
                    joint_limits_interface::PositionJointSoftLimitsHandle softhandle(*h, limits,soft_limits);
                    pos_soft_limits_interface_.registerHandle(softhandle);
                }
            }
            h = it->second->registerHandle(vel_interface_);
            if(h && has_joint_limits){
                joint_limits_interface::VelocityJointSaturationHandle sathandle(*h, limits);
                vel_saturation_interface_.registerHandle(sathandle);
                if(has_soft_limits){
                    joint_limits_interface::VelocityJointSoftLimitsHandle softhandle(*h, limits,soft_limits);
                    vel_soft_limits_interface_.registerHandle(softhandle);
                }
            }
            h = it->second->registerHandle(eff_interface_);
            if(h && has_joint_limits){
                joint_limits_interface::EffortJointSaturationHandle sathandle(*h, limits);
                eff_saturation_interface_.registerHandle(sathandle);
                if(has_soft_limits){
                    joint_limits_interface::EffortJointSoftLimitsHandle softhandle(*h, limits,soft_limits);
                    eff_soft_limits_interface_.registerHandle(softhandle);
                }
            }
        }
		return true;
    }
		
    virtual bool init() {
        boost::mutex::scoped_lock lock(mutex_);
        if(cm_) return false;

		cm_.reset(new controller_manager::ControllerManager(this, nh_));
        recover_ = true;
        return true;
    }
    virtual bool recover() {
        boost::mutex::scoped_lock lock(mutex_);
        if(!cm_) return false;
        recover_ = true;
        return true;
    }
    virtual bool shutdown(){
        boost::mutex::scoped_lock lock(mutex_);
        if(cm_) cm_.reset();
        return true;
    }
    void add(const std::string &name, boost::shared_ptr<HandleLayer> handle){
        handles_.insert(std::make_pair(name, handle));
    }

};

class MotorChain : RosChain<ThreadedSocketCANInterface, SharedMaster>{
    boost::shared_ptr< LayerGroup<MotorNode> > motors_;
    boost::shared_ptr< LayerGroup<HandleLayer> > handle_layer_;

    boost::shared_ptr< ControllerManagerLayer> cm_;

    virtual bool nodeAdded(XmlRpc::XmlRpcValue &module, const boost::shared_ptr<ipa_canopen::Node> &node)
    {
        std::string name = module["name"];
        boost::shared_ptr<MotorNode> motor( new MotorNode(node, name + "_motor"));
        motors_->add(motor);

        boost::shared_ptr<HandleLayer> handle( new HandleLayer(name, motor));
        handle_layer_->add(handle);
        cm_->add(name, handle);

        return true;
    }

public:
    MotorChain(const ros::NodeHandle &nh, const ros::NodeHandle &nh_priv): RosChain(nh, nh_priv){}
    
    virtual bool setup() {
        motors_.reset( new LayerGroup<MotorNode>("402 Layer"));
        handle_layer_.reset( new LayerGroup<HandleLayer>("Handle Layer"));
        cm_.reset(new ControllerManagerLayer(nh_));

        if(RosChain::setup()){
            boost::mutex::scoped_lock lock(mutex_);
            add(motors_);
            add(handle_layer_);

            add(cm_);

            return cm_->register_handles();
        }

        return false;
    }
};

int main(int argc, char** argv){
  ros::init(argc, argv, "ipa_canopen_chain_ros_node");
  ros::AsyncSpinner spinner(0);
  spinner.start();

  ros::NodeHandle nh;
  ros::NodeHandle nh_priv("~");

  MotorChain chain(nh, nh_priv);

  if(!chain.setup()){
      return -1;
  }

  ros::waitForShutdown();
  return 0;
}
