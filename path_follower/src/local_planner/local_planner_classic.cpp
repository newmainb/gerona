/// HEADER
#include <path_follower/local_planner/local_planner_classic.h>

/// PROJECT
#include <path_follower/pathfollower.h>

std::size_t LocalPlannerClassic::nnodes_ = 300;
int LocalPlannerClassic::ic_ = 3;
int LocalPlannerClassic::nsucc_ = 3;
int LocalPlannerClassic::li_level = 10;
std::vector<double> LocalPlannerClassic::RT;
std::vector<double> LocalPlannerClassic::D_THETA;
double LocalPlannerClassic::TH = 0.0;
double LocalPlannerClassic::length_MF = 1.0;
double LocalPlannerClassic::mu_ = 1.0;
std::vector<LNode> LocalPlannerClassic::EMPTYTWINS;

LocalPlannerClassic::LocalPlannerClassic(PathFollower &follower,
                                 tf::Transformer& transformer,
                                 const ros::Duration& update_interval)
    : LocalPlannerImplemented(follower, transformer, update_interval),
      d2p(0.0),last_s(0.0), new_s(0.0),velocity_(0.0),fvel_(false),b_obst(false),index1(-1), index2(-1),
      r_level(0), n_v(0), step_(0.0),stepc_(0.0),neig_s(0.0)
{

}

void LocalPlannerClassic::setGlobalPath(Path::Ptr path){
    LocalPlannerImplemented::setGlobalPath(path);
    last_s = 0.0;
    new_s = 0.0;
}

void LocalPlannerClassic::getSuccessors(LNode*& current, std::size_t& nsize, std::vector<LNode*>& successors,
                                        std::vector<LNode>& nodes, const std::vector<Constraint::Ptr>& constraints,
                                        const std::vector<bool>& fconstraints, std::vector<LNode>& twins, bool repeat){
    successors.clear();
    twins.resize(nsucc_);
    bool add_n = true;
    double ori = current->orientation;
    double trax = L*std::cos(ori)/2.0;
    double tray = L*std::sin(ori)/2.0;
    double ox = current->x - trax;
    double oy = current->y - tray;
    int j = 0;
    for(int i = 0; i < nsucc_; ++i){
        double x,y,theta,rt;
        if(i == 0){// straight
            theta = ori;
            x = ox + step_*std::cos(theta) + trax;
            y = oy + step_*std::sin(theta) + tray;
            rt = std::numeric_limits<double>::infinity();
        }else{
            if(i % 2 == 1){// right
                rt = -RT[j];
                theta = -D_THETA[j];
            }else{// left
                rt = RT[j];
                theta = D_THETA[j];
                j++;
            }
            theta = MathHelper::AngleClamp(ori + theta);
            trax = L*std::cos(theta)/2.0;
            tray = L*std::sin(theta)/2.0;
            x = ox + rt*(std::sin(theta)-std::sin(ori)) + trax;
            y = oy + rt*(-std::cos(theta)+std::cos(ori)) + tray;

        }
        LNode succ(x,y,theta,current,rt,current->level_+1);
        setDistances(succ);

        if(areConstraintsSAT(succ,constraints,fconstraints)){
            int wo = -1;
            if(!isInGraph(succ,nodes,nsize,wo)){
                if(add_n){
                    nodes.at(nsize) = succ;
                    successors.push_back(&nodes.at(nsize));
                    nsize++;
                    if(nsize >= nnodes_){
                        add_n = false;
                    }
                }
            }else{
                if(repeat){
                    twins.at(i) = succ;
                    nodes[wo].twin_ = &twins.at(i);
                    successors.push_back(&nodes[wo]);
                }
            }
        }
    }
}

bool LocalPlannerClassic::isInGraph(const LNode& current, std::vector<LNode>& nodes, std::size_t& asize, int& position){
    for(std::size_t i = 0; i < asize; ++i){
        double dis = current.distanceTo(nodes[i]);
        if(dis < neig_s){
            position = i;
            return true;
        }
    }
    return false;
}

void LocalPlannerClassic::setDistances(LNode& current){
    double closest_dist = std::numeric_limits<double>::infinity();
    std::size_t closest_index = 0;
    for(std::size_t i = index1; i <= index2; ++i) {
        const Waypoint& wp = waypoints[i];
        double dist = std::hypot(wp.x - current.x, wp.y - current.y);
        if(dist < closest_dist) {
            closest_dist = dist;
            closest_index = i;
        }
    }

    double dis = 0.0;
    if(closest_index == index1){
        while(closest_index != 0){
            const int c_i = closest_index - 1;
            const Waypoint& wp = waypoints[c_i];
            double dist = std::hypot(wp.x - current.x, wp.y - current.y);
            if(dist < closest_dist) {
                closest_dist = dist;
                closest_index = c_i;
            }else{
                break;
            }
        }
        if(closest_index == 0){
            const Waypoint& p0 = waypoints[0];
            const Waypoint& p1 = waypoints[1];
            double x = p1.x - p0.x;
            double y = p1.y - p0.y;
            double a_next = std::atan2(y,x);
            x = current.x - p0.x;
            y = current.y - p0.y;
            double a_point = std::atan2(y,x);
            double adiff = std::abs(MathHelper::AngleClamp(a_next - a_point));
            if(adiff > M_PI_2){
                double h = std::hypot(p0.x - current.x, p0.y - current.y);
                dis = h*std::cos(adiff);
                closest_dist = h*std::sin(adiff);
            }
        }
    }
    if(closest_index == index2){
        std::size_t last_p = waypoints.size() - 1;
        while(closest_index != last_p){
            const int c_i = closest_index + 1;
            const Waypoint& wp = waypoints[c_i];
            double dist = std::hypot(wp.x - current.x, wp.y - current.y);
            if(dist < closest_dist) {
                closest_dist = dist;
                closest_index = c_i;
            }else{
                break;
            }
        }
    }
    current.d2p = closest_dist;
    current.npp = waypoints[closest_index];
    current.s = current.npp.s + dis;

    if(b_obst){
        tf::Point pt(current.x, current.y, current.orientation);
        pt = odom_to_base * pt;
        double closest_obst = std::numeric_limits<double>::infinity();
        double closest_x = std::numeric_limits<double>::infinity();
        double closest_y = std::numeric_limits<double>::infinity();
        ObstacleCloud::const_iterator point_it;
        for (point_it = obstacle_cloud_->begin(); point_it != obstacle_cloud_->end(); ++point_it){
            double x = (double)(point_it->x) - pt.x();
            double y = (double)(point_it->y) - pt.y();
            double dist = std::hypot(x, y);
            if(dist < closest_obst) {
                closest_obst = dist;
                closest_x = (double)(point_it->x);
                closest_y = (double)(point_it->y);
            }
        }
        current.d2o = closest_obst;
        tf::Point tmpnop(closest_x ,closest_y,0.0);
        tmpnop = base_to_odom * tmpnop;
        current.nop = Waypoint(tmpnop.x(), tmpnop.y(), 0.0);
    }else{
        current.d2o = std::numeric_limits<double>::infinity();
    }
}

void LocalPlannerClassic::retrievePath(LNode* obj, SubPath& local_wps, double& l){
    LNode* cu = obj;
    r_level = cu->level_;
    l = 0.0;
    while(cu != nullptr){
        local_wps.push_back(*cu);
        if(local_wps.size() != 1){
            l += local_wps.back().distanceTo(local_wps.at(local_wps.size()-2));
        }
        if(cu->parent_ != nullptr){
            if(cu->radius_ != std::numeric_limits<double>::infinity()){
                const LNode* parent = cu->parent_;
                double theta = MathHelper::AngleClamp(cu->orientation - parent->orientation);
                if(std::abs(theta) > std::numeric_limits<double>::epsilon()){
                    const double rt = cu->radius_;
                    const double step = theta/((double)(ic_ + 1));
                    double ori = parent->orientation;
                    double trax = L*std::cos(ori)/2.0;
                    double tray = L*std::sin(ori)/2.0;
                    double ox = parent->x - trax;
                    double oy = parent->y - tray;
                    for(int i = ic_; i >= 1; --i){
                        theta = MathHelper::AngleClamp(ori + ((double)i)*step);
                        trax = L*std::cos(theta)/2.0;
                        tray = L*std::sin(theta)/2.0;
                        double x = ox + rt*(std::sin(theta)-std::sin(ori)) + trax;
                        double y = oy + rt*(-std::cos(theta)+std::cos(ori)) + tray;
                        Waypoint bc(x,y,theta);
                        bc.s = parent->s + ((double)i)*step*rt;
                        local_wps.push_back(bc);
                        l += local_wps.back().distanceTo(local_wps.at(local_wps.size()-2));
                    }
                }
            }
        }
        cu = cu->parent_;
    }
    std::reverse(local_wps.begin(),local_wps.end());
}

void LocalPlannerClassic::retrieveContinuity(LNode& wpose){
    global_path_.set_s_new(new_s);
    double curv;
    if(last_local_path_.n()>0){
        std::size_t index = -1;
        double closest_point = std::numeric_limits<double>::infinity();
        for(std::size_t i = 0; i < last_local_path_.n(); ++i){
            double x = last_local_path_.p(i) - wpose.x;
            double y = last_local_path_.q(i) - wpose.y;
            double dist = std::hypot(x, y);
            if(dist < closest_point) {
                closest_point = dist;
                index = i;
            }
        }
        curv = last_local_path_.curvature(index);
        setLLP(index + 1);
    }else{
        curv = global_path_.curvature(index1);
    }
    if(curv == 0.0){
        wpose.radius_ = std::numeric_limits<double>::infinity();
    }else{
        wpose.radius_ = 1.0/curv;
    }
}

void LocalPlannerClassic::setD2P(LNode& wpose){
    d2p = 2.0*wpose.d2p;
}

bool LocalPlannerClassic::processPath(LNode* obj,SubPath& local_wps){
    double length;
    retrievePath(obj, local_wps,length);
    if(length < 0.12){
        return false;
    }
    last_s = global_path_.s_new();
    std::size_t i_new = local_wps.size() - 1;
    double dis = velocity_ * update_interval_.toSec();
    for(std::size_t i = 1; i < local_wps.size(); ++i){
        if(local_wps.at(i).s - local_wps.at(0).s >= dis){
            i_new = i;
            break;
        }
    }
    global_path_.set_s_new(local_wps.at(i_new).s);
    smoothAndInterpolate(local_wps);
    last_local_path_.interpolatePath(local_wps, "/odom");
    local_wps = (SubPath)last_local_path_;
    if(tooClose){
        wlp_.wps.insert(wlp_.wps.end(),local_wps.begin(),local_wps.end());
    }
    return true;
}

void LocalPlannerClassic::setVelocity(geometry_msgs::Twist::_linear_type vector){
    if(!fvel_){
        double tmpv = sqrt(vector.x*vector.x + vector.y*vector.y + vector.z*vector.z);
        if(tmpv > 0.3){
            n_v++;
            velocity_ += (tmpv - velocity_)/(double)n_v;
            setStep();
        }
    }
}

void LocalPlannerClassic::setVelocity(double velocity){
    velocity_ = velocity;
    n_v = 1;
    fvel_ = true;
    setStep();
}

void LocalPlannerClassic::setStep(){
    double dis = velocity_ * update_interval_.toSec();
    step_ = length_MF*dis/(double)li_level;
    D_THETA.clear();
    for(std::size_t i = 0; i < RT.size(); ++i){
        D_THETA.push_back(MathHelper::AngleClamp(step_/RT[i]));
    }
    double H_D_THETA = D_THETA.front()/2.0;
    double l_step = 2.0*RT.front()*std::sin(H_D_THETA);
    neig_s = l_step*(H_D_THETA > M_PI_4?std::cos(H_D_THETA):std::sin(H_D_THETA));
    Dis2Path_Constraint::setDRate(neig_s);
    stepc_ = 2.0*RT.back()*std::sin(D_THETA.back()/2.0);
    double vdis = velocity_*velocity_/(2.0*9.81*mu_);
    Dis2Path_Constraint::setVDis(vdis);
    Dis2Obst_Constraint::setVDis(vdis);
    Dis2Obst_Scorer::setVDis(vdis);
}

//borrowed from path_planner/planner_node.cpp
SubPath LocalPlannerClassic::interpolatePath(const SubPath& path, double max_distance){
    unsigned n = path.size();
    SubPath result;
    if(n < 2) {
        return result;
    }

    result.push_back(path[0]);

    for(unsigned i = 1; i < n; ++i){
        const Waypoint* current = &path[i];
        // split the segment, iff it is to large
        subdividePath(result, result.back(), *current, max_distance);
        // add the end of the segment (is not done, when splitting)
        result.push_back(*current);
    }
    return result;
}

//borrowed from path_planner/planner_node.cpp
void LocalPlannerClassic::subdividePath(SubPath& result, Waypoint low, Waypoint up, double max_distance){
    double distance = low.distanceTo(up);
    if(distance > max_distance) {
        // split half way between the lower and the upper node
        Waypoint halfway(low.x,low.y,low.orientation);

        halfway.x += (up.x - low.x) / 2.0;
        halfway.y += (up.y - low.y) / 2.0;

        halfway.orientation = (up.orientation + low.orientation) / 2.0;
        // first recursive descent in lower part
        subdividePath(result, low, halfway, max_distance);
        // then add the half way point
        result.push_back(halfway);
        // then descent in upper part
        subdividePath(result, halfway, up, max_distance);
    }
}

//borrowed from path_planner/planner_node.cpp
SubPath LocalPlannerClassic::smoothPath(const SubPath& path, double weight_data, double weight_smooth, double tolerance){
    SubPath result;
    int n = path.size();
    if(n < 2) {
        return result;
    }
    // find segments
    std::vector<SubPath> segments = segmentPath(path);
    bool first = true;
    // smooth segments and merge results
    for(const SubPath& segment : segments) {
        SubPath smoothed_segment = smoothPathSegment(segment, weight_data, weight_smooth, tolerance);
        if(first){
            first = false;
            result.wps.insert(result.end(), smoothed_segment.begin(), smoothed_segment.end());
        }else{
            if(smoothed_segment.size() > 1){
                result.wps.insert(result.end(), smoothed_segment.begin() + 1, smoothed_segment.end());
            }
        }
    }

    return result;
}

//borrowed from path_planner/planner_node.cpp
std::vector<SubPath> LocalPlannerClassic::segmentPath(const SubPath &path){
                                   std::vector<SubPath> result;

                                   int n = path.size();
                                   if(n < 2) {
                                       return result;
                                   }

                                   SubPath current_segment;

                                   const Waypoint * last_point = &path[0];
                                   current_segment.push_back(*last_point);

                                   for(int i = 1; i < n; ++i){
                                       const Waypoint* current_point = &path[i];

                                       // append to current segment
                                       current_segment.push_back(*current_point);

                                       bool is_the_last_node = i == n-1;
                                       bool segment_ends_with_this_node = false;

                                       if(is_the_last_node) {
                                           // this is the last node
                                           segment_ends_with_this_node = true;

                                       } else {
                                           const Waypoint* next_point = &path[i+1];

                                           // if angle between last direction and next direction to large -> segment ends
                                           double diff_last_x = current_point->x - last_point->x;
                                           double diff_last_y = current_point->y - last_point->y;
                                           double last_angle = std::atan2(diff_last_y, diff_last_x);

                                           double diff_next_x = next_point->x - current_point->x;
                                           double diff_next_y = next_point->y - current_point->y;
                                           double next_angle = std::atan2(diff_next_y, diff_next_x);

                                           if(std::abs(MathHelper::AngleClamp(last_angle - next_angle)) > M_PI / 2.0) {
                                               // new segment!
                                               // current node is the last one of the old segment
                                               segment_ends_with_this_node = true;
                                           }
                                       }

                                       if(segment_ends_with_this_node) {
                                           result.push_back(current_segment);

                                           current_segment.wps.clear();

                                           if(!is_the_last_node) {
                                               // begin new segment

                                               // current node is also the first one of the new segment
                                               current_segment.push_back(*current_point);
                                           }
                                       }

                                       last_point = current_point;
                                   }

                                   return result;
}

//borrowed from path_planner/planner_node.cpp
SubPath LocalPlannerClassic::smoothPathSegment(const SubPath& path, double weight_data, double weight_smooth, double tolerance){
    SubPath new_path(path);

    unsigned n = path.size();
    if(n < 2) {
        return new_path;
    }

    double last_change = -2 * tolerance;
    double change = 0;

    int offset = 2;

    while(change > last_change + tolerance) {
        last_change = change;
        change = 0;
        Waypoint origin(0,0,0);

        for(unsigned i = offset; i < n-offset; ++i){
            Waypoint path_i = path[i];
            Waypoint new_path_i = new_path[i];
            Waypoint new_path_ip1 = new_path[i+1];
            Waypoint new_path_im1 = new_path[i-1];

            Waypoint deltaData(weight_data*(path_i.x - new_path_i.x),
                               weight_data*(path_i.y - new_path_i.y),0.0);

            new_path_i.x = new_path_i.x + deltaData.x;
            new_path_i.y = new_path_i.y + deltaData.y;

            Waypoint deltaSmooth(weight_smooth * (new_path_ip1.x + new_path_im1.x - 2* new_path_i.x),
                                 weight_smooth * (new_path_ip1.y + new_path_im1.y - 2* new_path_i.y),
                                 0.0);
            new_path_i.x = new_path_i.x + deltaSmooth.x;
            new_path_i.y = new_path_i.y + deltaSmooth.y;

            new_path[i].x = new_path_i.x;
            new_path[i].y = new_path_i.y;


            change += deltaData.distanceTo(origin) + deltaSmooth.distanceTo(origin);
        }
    }

    // update orientations
    double a = new_path[0].orientation;
    Waypoint current = new_path[0];
    Waypoint next = new_path[1];

    double dx = next.x - current.x;
    double dy = next.y - current.y;

    Eigen::Vector2d looking_dir_normalized(std::cos(a), std::sin(a));
    Eigen::Vector2d delta(dx, dy);
    const double theta_diff = std::acos(delta.dot(looking_dir_normalized) / delta.norm());

    // decide whether to drive forward or backward
    bool is_backward = (theta_diff > M_PI_2 || theta_diff < -M_PI_2) ;

    for(unsigned i = 1; i < n-1; ++i){
        Waypoint next = new_path[i+1];
        Waypoint prev = new_path[i-1];

        Waypoint delta(next.x - prev.x, next.y - prev.y, 0.0);
        double angle = std::atan2(delta.y, delta.x);

        if(is_backward) {
            angle = MathHelper::AngleClamp(angle + M_PI);
        }

        new_path[i].orientation = angle;
    }

    return new_path;
}

void LocalPlannerClassic::initIndexes(Eigen::Vector3d& pose){
    double closest_dist = std::numeric_limits<double>::infinity();
    if(last_s == global_path_.s_new()){
        for(std::size_t i = 0; i < global_path_.n(); ++i){
            if(global_path_.s(i) > last_s){
                index1 = i == 0?0:i-1;
                double x = global_path_.p(index1) - pose(0);
                double y = global_path_.q(index1) - pose(1);
                closest_dist = std::hypot(x, y);
                break;
            }
        }
    }else{
        std::size_t j = 0;
        std::size_t last_i, first_i;
        int first_c = -1;
        index1 = j;
        double c_s = global_path_.s(j);
        double g1,g2;
        if(last_s > global_path_.s_new()){
            g1 = global_path_.s_new();
            g2 = last_s;
        }else{
            g1 = last_s;
            g2 = global_path_.s_new();
        }
        while(c_s <= g2){
            if(c_s >= g1){
                if(first_c == -1){
                    first_c = j;
                }
                double x = global_path_.p(j) - pose(0);
                double y = global_path_.q(j) - pose(1);
                double dist = std::hypot(x, y);
                if(dist < closest_dist) {
                    closest_dist = dist;
                    index1 = j;
                }
                last_i = j;
            }
            ++j;
            if(j >= global_path_.n()){
                break;
            }
            c_s = global_path_.s(j);
        }
        if(first_c != -1){
            first_i = first_c;
            if(index1 == first_i){
                while(index1 != 0){
                    const std::size_t c_i = index1 - 1;
                    double x = global_path_.p(c_i) - pose(0);
                    double y = global_path_.q(c_i) - pose(1);
                    double dist = std::hypot(x, y);
                    if(dist < closest_dist) {
                        closest_dist = dist;
                        index1 = c_i;
                    }else{
                        break;
                    }
                }
            }
            if(index1 == last_i){
                std::size_t last_p = global_path_.n() - 1;
                while(index1 != last_p){
                    const std::size_t c_i = index1 + 1;
                    double x = global_path_.p(c_i) - pose(0);
                    double y = global_path_.q(c_i) - pose(1);
                    double dist = std::hypot(x, y);
                    if(dist < closest_dist) {
                        closest_dist = dist;
                        index1 = c_i;
                    }else{
                        break;
                    }
                }
            }
        }
    }
    double s_new = global_path_.s(index1) + length_MF * velocity_ * update_interval_.toSec();
    closest_dist = std::numeric_limits<double>::infinity();
    for(std::size_t i = index1; i < global_path_.n(); ++i){
        double dist = std::abs(s_new - global_path_.s(i));
        if(dist < closest_dist) {
            closest_dist = dist;
            index2 = i;
        }
    }

    new_s = global_path_.s(index1);
}

void LocalPlannerClassic::initConstraints(const std::vector<Constraint::Ptr>& constraints,
                                          const std::vector<bool>& fconstraints){
    if(fconstraints.at(0)){
        std::dynamic_pointer_cast<Dis2Path_Constraint>(constraints.at(0))->setParams(d2p);
    }
}

void LocalPlannerClassic::setNormalizer(const std::vector<Constraint::Ptr>& constraints,
                                        const std::vector<bool>& fconstraints){
    if(fconstraints.at(0)){
        double n_limit = std::dynamic_pointer_cast<Dis2Path_Constraint>(constraints.at(0))->getLimit();
        Dis2PathP_Scorer::setMaxD(n_limit);
        Dis2PathD_Scorer::setMaxD(n_limit);
    }
}

bool LocalPlannerClassic::areConstraintsSAT(const LNode& current, const std::vector<Constraint::Ptr>& constraints,
                                            const std::vector<bool>& fconstraints){
    bool rval = true;
    for(std::size_t i = 0; i < constraints.size(); ++i){
        if(fconstraints.at(i)){
            rval = rval && constraints.at(i)->isSatisfied(current);
        }
    }
    return rval;
}

void LocalPlannerClassic::smoothAndInterpolate(SubPath& local_wps){
    //interpolate
    local_wps = interpolatePath(local_wps, 0.5);
    //smoothing
    local_wps = smoothPath(local_wps, 0.6, 0.15);
    //final interpolate
    local_wps = interpolatePath(local_wps, 0.1);
    //final smoothing
    local_wps = smoothPath(local_wps, 2.0, 0.4);
}

void LocalPlannerClassic::printNodeUsage(std::size_t& nnodes) const{
    ROS_INFO_STREAM("# Nodes: " << nnodes);
}

double LocalPlannerClassic::Heuristic(const LNode& current, const double& dis2last){
    return dis2last - current.s;
}

double LocalPlannerClassic::Cost(const LNode& current, const std::vector<Scorer::Ptr>& scorer,
                                 const std::vector<double>& wscorer, double& score){
    double cost = 0.0;
    if(current.parent_ != nullptr){
        if(current.radius_ == std::numeric_limits<double>::infinity()){
            cost += step_;
        }else{
            const double theta = MathHelper::AngleClamp(current.orientation - current.parent_->orientation);
            cost += current.radius_*theta;
        }
    }
    score = Score(current,scorer,wscorer);
    cost += score;
    return cost;
}

double LocalPlannerClassic::Score(const LNode& current, const std::vector<Scorer::Ptr>& scorer,
                                  const std::vector<double>& wscorer){
    double score = 0.0;
    for(std::size_t i = 0; i < scorer.size(); ++i){
        score += ((wscorer.at(i) != 0.0)?(wscorer.at(i)*scorer.at(i)->score(current)):0.0);
    }
    return score;
}

void LocalPlannerClassic::setLLP(std::size_t index){
    SubPath tmp_p = (SubPath)last_local_path_;
    wlp_.wps.clear();
    wlp_.wps.assign(tmp_p.begin(),tmp_p.begin() + index);
}

void LocalPlannerClassic::setLLP(){
    setLLP(last_local_path_.n());
}

void LocalPlannerClassic::setParams(int nnodes, int ic, double dis2p, double dis2o, double s_angle,
                                    int ia, double lmf, int max_level, double mu, double ef){
    nnodes_ = nnodes;
    ic_ = ic;
    TH = s_angle*M_PI/180.0;
    length_MF = lmf;
    mu_ = mu;
    li_level = max_level;
    RT.clear();
    for(int i = 0; i <= ia; ++i){
        RT.push_back(L/std::tan(((double)(i + 1)/(ia + 1))*TH));
    }
    nsucc_ = 2*RT.size() + 1;
    Curvature_Scorer::setMaxC(RT.back());
    CurvatureD_Scorer::setMaxC(RT.back());
    Level_Scorer::setLevel(li_level);
    Dis2Path_Constraint::setLimits(dis2p,dis2o);
    Dis2Obst_Constraint::setLimit(dis2o);
    Dis2Obst_Scorer::setLimit(dis2o);
    Dis2Obst_Scorer::setFactor(ef);
}

void LocalPlannerClassic::printVelocity(){
    ROS_INFO_STREAM("Mean velocity: " << velocity_ << " m/s");
    if(fvel_){
        fvel_ = false;
    }
}

void LocalPlannerClassic::printLevelReached() const{
    ROS_INFO_STREAM("Reached Level: " << r_level << "/" << li_level);
}

bool LocalPlannerClassic::createAlternative(LNode*& s_p, LNode& alt, const std::vector<Constraint::Ptr>& constraints,
                                            const std::vector<bool>& fconstraints, bool allow_lines){
    LNode* s = s_p->parent_;
    bool line = false;
    if(s->parent_ == nullptr){
        return false;
    }
    LNode* parent = s->parent_;
    double trx1 = L*std::cos(s->orientation)/2.0;
    double try1 = L*std::sin(s->orientation)/2.0;
    double trx2 = L*std::cos(parent->orientation)/2.0;
    double try2 = L*std::sin(parent->orientation)/2.0;
    double x = (s->x - trx1) - (parent->x - trx2);
    double y = (s->y - try1) - (parent->y - try2);
    double d = std::hypot(x,y);
    double theta_b = std::atan2(y,x);
    double trx3 = L*std::cos(s_p->orientation)/2.0;
    double try3 = L*std::sin(s_p->orientation)/2.0;
    x = (s_p->x - trx3) - (s->x - trx1);
    y = (s_p->y - try3) - (s->y - try1);
    double c = std::hypot(x,y);
    double theta_r = std::atan2(y,x);
    double gamma  = MathHelper::AngleClamp(theta_r - theta_b);
    double theta_p = MathHelper::AngleClamp(theta_b - parent->orientation);
    double divisor = c*std::sin(MathHelper::AngleClamp(gamma + theta_p)) + d*std::sin(theta_p);
    if(std::abs(divisor) <= std::numeric_limits<double>::epsilon()){
        if(!allow_lines){
            return false;
        }else{
            line = true;
        }
    }
    divisor *= 2.0;
    x = (s_p->x - trx3) - (parent->x - trx2);
    y = (s_p->y - try3) - (parent->y - try2);
    double a = std::hypot(x,y);
    double theta_dir = std::atan2(y,x);
    if(std::abs(MathHelper::AngleClamp(theta_dir - parent->orientation)) > M_PI_2){
        return false;
    }
    if(line){
        alt = *s_p;
        alt.orientation = parent->orientation;
        alt.parent_ = parent;
        alt.radius_ = std::numeric_limits<double>::infinity();;
        return true;
    }
    double R = (a*a)/divisor;
    double psi_v = atan2(L,std::abs(R));
    if (psi_v > TH){
        return false;
    }
    double theta_n = (R >= 0.0?1.0:-1.0)*std::acos(1-(0.5*a*a)/(R*R));
    alt = *s_p;
    alt.x -= trx3;
    alt.y -= try3;
    alt.orientation = MathHelper::AngleClamp(parent->orientation + theta_n);
    alt.x += L*std::cos(alt.orientation)/2.0;
    alt.y += L*std::sin(alt.orientation)/2.0;
    alt.parent_ = parent;
    alt.radius_ = R;
    setDistances(alt);
    return areConstraintsSAT(alt,constraints,fconstraints);
}

bool LocalPlannerClassic::algo(Eigen::Vector3d& pose, SubPath& local_wps,
                                  const std::vector<Constraint::Ptr>& constraints,
                                  const std::vector<Scorer::Ptr>& scorer,
                                  const std::vector<bool>& fconstraints,
                                  const std::vector<double>& wscorer,
                                  std::size_t& nnodes){
    initIndexes(pose);
    b_obst = fconstraints.back() || wscorer.back() != 0;

    LNode wpose(pose(0),pose(1),pose(2),nullptr,std::numeric_limits<double>::infinity(),0);
    setDistances(wpose);

    double dis2last = global_path_.s(global_path_.n()-1);

    if(std::abs(dis2last - wpose.s) < 0.8){
        tooClose = true;
        setLLP();
        return false;
    }

    retrieveContinuity(wpose);
    setD2P(wpose);
    initConstraints(constraints,fconstraints);

    std::vector<LNode> nodes(nnodes_);
    LNode* obj = nullptr;

    setNormalizer(constraints,fconstraints);

    setInitScores(wpose, scorer, wscorer, dis2last);

    nodes.at(0) = wpose;

    initQueue(nodes[0]);
    initLeaves(nodes[0]);
    double best_p = std::numeric_limits<double>::infinity();
    nnodes = 1;

    LNode* current;

    while(!isQueueEmpty() && (isQueueEmpty()?nodes.at(nnodes - 1).level_:queueFront()->level_) < li_level && nnodes < nnodes_){
        pop(current);
        if(std::abs(dis2last - current->s) <= 0.05){
            obj = current;
            tooClose = true;
            break;
        }
        push2Closed(current);

        std::vector<LNode*> successors;
        expandCurrent(current, nnodes, successors, nodes, constraints, fconstraints);
        setNormalizer(constraints,fconstraints);
        updateLeaves(successors, current);
        for(std::size_t i = 0; i < successors.size(); ++i){
            double current_p;
            if(!processSuccessor(successors[i], current, current_p, dis2last, constraints, scorer, fconstraints, wscorer)){
                continue;
            }
            addLeaf(successors[i]);
            updateBest(current_p,best_p,obj,successors[i]);
        }
    }
    reconfigureTree(obj, nodes, best_p, constraints, scorer, fconstraints,wscorer);
    if(obj != nullptr){
        return processPath(obj, local_wps);
    }else{
        return false;
    }
}

