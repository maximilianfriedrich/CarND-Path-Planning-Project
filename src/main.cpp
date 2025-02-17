#include <uWS/uWS.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
#include "spline.h"

// for convenience
using nlohmann::json;
using std::string;
using std::vector;

using namespace std;

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;
  
   // start in lane 1
   int lane = 1;
   int target_lane = lane;
   // have a reference velocity to target
   double ref_vel = 0.0; // mph
   const double speed_limit = 50;
   double target_speed = 48;




  std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);

  string line;
  while(getline(in_map_, line)) {
    std::istringstream iss(line);
    double x;
    double y;
    float s;
    float d_x;
    float d_y;
    iss >> x;
    iss >> y;
    iss >> s;
    iss >> d_x;
    iss >> d_y;
    map_waypoints_x.push_back(x);
    map_waypoints_y.push_back(y);
    map_waypoints_s.push_back(s);
    map_waypoints_dx.push_back(d_x);
    map_waypoints_dy.push_back(d_y);
  }

  h.onMessage([&lane,&target_lane,
			   &ref_vel,
			   &speed_limit,
			   &target_speed,
			   &map_waypoints_x,&map_waypoints_y,&map_waypoints_s,
               &map_waypoints_dx,&map_waypoints_dy]
             (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
               uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    if(length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if(s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if(event == "telemetry") {
          // j[1] is the data JSON object
          
          // Main car's localization Data
          double car_x = j[1]["x"];
          double car_y = j[1]["y"];
          double car_s = j[1]["s"];
          double car_d = j[1]["d"];
          double car_yaw = j[1]["yaw"];
          double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          auto previous_path_x = j[1]["previous_path_x"];
          auto previous_path_y = j[1]["previous_path_y"];
          // Previous path's end s and d values 
          double end_path_s = j[1]["end_path_s"];
          double end_path_d = j[1]["end_path_d"];

          // Sensor Fusion Data, a list of all other cars on the same side 
          //   of the road.
          auto sensor_fusion = j[1]["sensor_fusion"];

		  int prev_size = previous_path_x.size();
		  
		  //check if there are vehicles in the danger zone
		  bool left = false;
		  bool right = false;
		  bool ego = false;
		  double closest_s = 100;
		  target_speed=48;
		  for (int i=0;i< sensor_fusion.size();i++)
			  {  
				// check position and speed of each observed vehicle
                double vx = sensor_fusion[i][3];
                double vy = sensor_fusion[i][4];
                double global_speed = sqrt ( vx * vx + vy * vy );
				global_speed += ( ( double ) prev_size * 0.02 * global_speed );
                // global position of the vehicle in frenet coordinates
                double global_vehicle_s = sensor_fusion[i][5];
                double global_vehicle_d = sensor_fusion[i][6];				  
				
				//relative position of target vehicle to ego vehicle	
				double relative_s_vehicle = global_vehicle_s - car_s;
                double relative_d_vehicle = global_vehicle_d - car_d;
		
				//check ahead and behind the ego vehicle
				if(relative_s_vehicle < 30 && relative_s_vehicle > -60) 
					{
					//check if target vehicle is left, right or in ego lane
					if(relative_d_vehicle > 2 && relative_d_vehicle < 6) {
                                right = true;
                            }

                    else if(relative_d_vehicle < -2 && relative_d_vehicle > -6) {
                                left = true;
                            }

                    }
				if(relative_s_vehicle < 30 && relative_s_vehicle > 0) 
					{
					if(relative_d_vehicle > -2 && relative_d_vehicle < 2) {
						ego = true;
								
						if (relative_s_vehicle<closest_s)
							{
								closest_s=relative_s_vehicle;
								target_speed= global_speed;
							}	
							
						}
					}
				    
			  }

				//lane change
				bool turn_left = false;
				bool turn_right = false;
				bool ready =false;
				if (car_speed < 45.0 && closest_s < 30.0 && closest_s > 10.0)
                    ready=true;
				
        //std::cout << "target speed " << target_speed << "current speed " << ref_vel << std::endl;
			// speed regulation
				/*
				if (closest_s < 30.0)
				{
                    ref_vel -= min(0.19,10.0/closest_s);
                }
				*/
				
				if ( ref_vel < target_speed ) 
				{
                    ref_vel += 0.19;
                }
				
				else if ( ref_vel > target_speed ) 
				{
                    ref_vel -= 0.19;
                }
				
				
				if (ego==true && ready ==true && lane== target_lane)
				{
				// left lane is open
					if ( lane >0 && left==false)
					{
						turn_left = true;
						lane -=1;
						target_lane=lane;
					}

					// right lane is open
					else if ( lane <2 && right==false )
					{
						turn_right = true;
						lane +=1;
						target_lane=lane;
					}
				}

                else if ( ref_vel > speed_limit ) 
				{
                    ref_vel -= 0.19;
                }


				
				


				
				

                    vector<double> ptsx;
                    vector<double> ptsy;

                    // reference x, y, yaw states
                    // either we will reference the starting point as where
                    // the car is or at the previous paths end point
                    double ref_x = car_x;
                    double ref_y = car_y;
                    double ref_yaw = deg2rad ( car_yaw );

                    // if previous size is almost empty, use the car as starting reference.
                    if ( prev_size < 2 ) {
                        // use two points that make the path tangent to the car
                        double prev_car_x = car_x - cos ( car_yaw );
                        double prev_car_y = car_y - sin ( car_yaw );

                        ptsx.push_back ( prev_car_x );
                        ptsy.push_back ( prev_car_y );

                        ptsx.push_back ( car_x );
                        ptsy.push_back ( car_y );
                    }

                    // use the previous paths end point as starting reference
                    else {
                        // redefine reference state as previous path end point
                        ref_x = previous_path_x[prev_size - 1];
                        ref_y = previous_path_y[prev_size - 1];

                        double ref_x_prev = previous_path_x[prev_size - 2];
                        double ref_y_prev = previous_path_y[prev_size - 2];
                        ref_yaw = atan2 ( ref_y - ref_y_prev, ref_x - ref_x_prev );

                        // use two points that make the path tangent to the previous paths end point
                        ptsx.push_back ( ref_x_prev );
                        ptsy.push_back ( ref_y_prev );

                        ptsx.push_back ( ref_x );
                        ptsy.push_back ( ref_y );
                    }

                    // In Frenet add evenly 30m space points ahead of the starting reference
                    vector<double> next_wp0 = getXY ( car_s + 30, ( 2 + 4 * lane ), map_waypoints_s, map_waypoints_x, map_waypoints_y );
                    vector<double> next_wp1 = getXY ( car_s + 60, ( 2 + 4 * lane ), map_waypoints_s, map_waypoints_x, map_waypoints_y );
                    vector<double> next_wp2 = getXY ( car_s + 90, ( 2 + 4 * lane ), map_waypoints_s, map_waypoints_x, map_waypoints_y );

                    ptsx.push_back ( next_wp0[0] );
                    ptsy.push_back ( next_wp0[1] );

                    ptsx.push_back ( next_wp1[0] );
                    ptsy.push_back ( next_wp1[1] );

                    ptsx.push_back ( next_wp2[0] );
                    ptsy.push_back ( next_wp2[1] );

                    for ( int i = 0; i < ptsx.size(); i++ ) {
                        // shift car reference angle to 0 degrees
                        double shift_x = ptsx[i] - ref_x;
                        double shift_y = ptsy[i] - ref_y;

                        ptsx[i] = ( shift_x * cos ( 0 - ref_yaw ) - shift_y * sin ( 0 - ref_yaw ) );
                        ptsy[i] = ( shift_x * sin ( 0 - ref_yaw ) + shift_y * cos ( 0 - ref_yaw ) );
                    }

                    // create spline
                    tk::spline s;

                    // set(x, y) points to the spline
                    s.set_points ( ptsx, ptsy );

                    // define the actual (x, y) points we will use for the Planner
                    vector<double> next_x_vals;
                    vector<double> next_y_vals;

                    // start with all of the previous path points from the last time
                    for ( int i = 0; i < previous_path_x.size(); i++ ) {
                        next_x_vals.push_back ( previous_path_x[i] );
                        next_y_vals.push_back ( previous_path_y[i] );
                    }

                    // Calculate how to break up spline points so that we travel at our desired reference velocity
                    double target_x = 30.0;
                    double target_y = s ( target_x );
                    double target_dist = sqrt ( ( target_x ) * ( target_x ) + ( target_y ) * ( target_y ) );

                    double x_add_on = 0;

                    // Fill up the rest of our path plannner after filling it with previous points here we will output 50 points
                    for ( int i = 1; i <= 50 - previous_path_x.size(); i++ ) {
                        double N = ( target_dist / ( 0.02 * ref_vel / 2.24 ) );
                        double x_point = x_add_on + ( target_x ) / N;
                        double y_point = s ( x_point );

                        x_add_on = x_point;

                        double x_ref = x_point;
                        double y_ref = y_point;

                        // rotate back to normal after rotating it earlier
                        x_point = ( x_ref * cos ( ref_yaw ) - y_ref * sin ( ref_yaw ) );
                        y_point = ( x_ref * sin ( ref_yaw ) + y_ref * cos ( ref_yaw ) );

                        x_point += ref_x;
                        y_point += ref_y;

                        next_x_vals.push_back ( x_point );
                        next_y_vals.push_back ( y_point );
                    }

                    json msgJson;
                    msgJson["next_x"] = next_x_vals;
                    msgJson["next_y"] = next_y_vals;

                    auto msg = "42[\"control\"," + msgJson.dump() + "]";

                    //this_thread::sleep_for(chrono::milliseconds(1000));
                    ws.send ( msg.data(), msg.length(), uWS::OpCode::TEXT );
                }
            } else {
                // Manual driving
                std::string msg = "42[\"manual\",{}]";
                ws.send ( msg.data(), msg.length(), uWS::OpCode::TEXT );
            }
        }
    } );

    // We don't need this since we're not using HTTP but if it's removed the
    // program
    // doesn't compile :-(
    h.onHttpRequest ( [] ( uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                      size_t, size_t ) {
        const std::string s = "<h1>Hello world!</h1>";
        if ( req.getUrl().valueLength == 1 ) {
            res->end ( s.data(), s.length() );
        } else {
            // i guess this should be done more gracefully?
            res->end ( nullptr, 0 );
        }
    } );

    h.onConnection ( [&h] ( uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req ) {
        std::cout << "Connected!!!" << std::endl;
    } );

    h.onDisconnection ( [&h] ( uWS::WebSocket<uWS::SERVER> ws, int code,
                        char *message, size_t length ) {
        ws.close();
        std::cout << "Disconnected" << std::endl;
    } );

    int port = 4567;
    if ( h.listen ( port ) ) {
        std::cout << "Listening to port " << port << std::endl;
    } else {
        std::cerr << "Failed to listen to port" << std::endl;
        return -1;
    }
    h.run();
}

