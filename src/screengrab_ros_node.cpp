/*
 * Copyright (c) 2013 Lucas Walter 
 * November 2013
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <dynamic_reconfigure/server.h>
#include <nodelet/nodelet.h>
#include <ros/ros.h>
#include <screen_grab/ScreenGrabConfig.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/RegionOfInterest.h>

// X Server includes
#include <X11/Xlib.h>
#include <X11/Xutil.h>

void XImage2RosImage(XImage& ximage, Display& _xDisplay, Screen& _xScreen,
    sensor_msgs::ImagePtr& im) 
{
    XColor color;
    
    im->header.stamp = ros::Time::now();

    if (_xScreen.depths->depth == 24) {
       // the code just deleted here is probably more robust than
       // a straight memcpy, but for now go without it.
       const int wd = ximage.width;
       const int ht = ximage.height;
       const int frame_size = wd * ht * 4;
       im->width = wd;
       im->height = ht;
       im->step = im->width * 4;
       // maybe this could be extracted from X
       im->encoding = sensor_msgs::image_encodings::BGRA8;
       im->data.resize(frame_size);
       memcpy(&im->data[0], ximage.data, frame_size); 

    } else { // Extremly slow alternative for non 24bit-depth
        Colormap colmap = DefaultColormap(&_xDisplay, DefaultScreen(&_xDisplay));
        for (unsigned int x = 0; x < ximage.width; x++) {
            for (unsigned int y = 0; y < ximage.height; y++) {
                color.pixel = XGetPixel(&ximage, x, y);
                XQueryColor(&_xDisplay, colmap, &color);
                //cv::Vec4b col = cv::Vec4b(color.blue, color.green, color.red,0);
                //tmp.at<cv::Vec4b> (y,x) = col;
            }
        }
    }
    return;
}

namespace screen_grab
{

class ScreenGrab : public nodelet::Nodelet
{
  //ros::NodeHandle nh_;
  
  ros::Publisher screen_pub_;

  ros::Subscriber roi_sub_;
  void roiCallback(const sensor_msgs::RegionOfInterest::ConstPtr& msg);

  int update_rate_;

  typedef dynamic_reconfigure::Server<screen_grab::ScreenGrabConfig> ReconfigureServer;
  boost::shared_ptr< ReconfigureServer > server_;
  void callback(screen_grab::ScreenGrabConfig &config,
      uint32_t level);

  void checkRoi(int& x_offset, int& y_offset, int& width, int& height);
  void updateConfig();

  int x_offset_;
  int y_offset_;
  int width_;
  int height_;
  
  int screen_w_;
  int screen_h_;
  
  ros::Rate loop_rate_;

  boost::recursive_mutex dr_mutex_;

public:

  virtual void onInit();

  ScreenGrab();
  
  bool spin();

};

} // screen_grab

#include <pluginlib/class_list_macros.h>

PLUGINLIB_EXPORT_CLASS(screen_grab::ScreenGrab, nodelet::Nodelet)

namespace screen_grab
{

ScreenGrab::ScreenGrab() :
    x_offset_(0),
    y_offset_(0),
    width_(640),
    height_(480),
    loop_rate_(15)
    //server_(dr_mutex_) // this locks up
{

}

void ScreenGrab::onInit()
{
  screen_pub_ = getPrivateNodeHandle().advertise<sensor_msgs::Image>(
      "image", 5);

  server_.reset(new ReconfigureServer(dr_mutex_, getPrivateNodeHandle())); 

  dynamic_reconfigure::Server<screen_grab::ScreenGrabConfig>::CallbackType cbt =
      boost::bind(&ScreenGrab::callback, this, _1, _2);
  server_->setCallback(cbt);

  roi_sub_ = getPrivateNodeHandle().subscribe("roi", 0, &ScreenGrab::roiCallback, this);
  
  spin();
}
  
void ScreenGrab::roiCallback(const sensor_msgs::RegionOfInterest::ConstPtr& msg)
{
  x_offset_ = msg->x_offset;
  y_offset_ = msg->y_offset;
  width_ = msg->width;
  height_ = msg->height;

  updateConfig();
}

void ScreenGrab::checkRoi(int& x_offset, int& y_offset, int& width, int& height)
{
  // TODO with cv::Rect this could be one line rect1 & rect2
  
  // Need to check against resolution
  if ((x_offset + width) > screen_w_) 
  {
    // TBD need to more intelligently cap these
    if (screen_w_ > width) 
    {
      x_offset = screen_w_ - width;
    } 
    else 
    {
      x_offset = 0;
      width = screen_w_;
    }
  }

  if ((y_offset + height) > screen_h_) 
  {
    // TBD need to more intelligently cap these
    if (screen_h_ > height) 
    {
      y_offset = screen_h_ - height;
    } 
    else 
    {
      y_offset = 0;
      height = screen_h_;
    }
  }

}

void ScreenGrab::callback(
    screen_grab::ScreenGrabConfig &config,
    uint32_t level)
{
  if (level & 1)
  {
    checkRoi(config.x_offset, config.y_offset, config.width, config.height);
    x_offset_ = config.x_offset;
    y_offset_ = config.y_offset;
    width_ = config.width;
    height_ = config.height;
  }
  
  if (level & 2)
  {
    if (config.update_rate != update_rate_) 
    {
      loop_rate_ = ros::Rate(config.update_rate);
      update_rate_ = config.update_rate;
    }
  }
}

void ScreenGrab::updateConfig()
{
  checkRoi(x_offset_, y_offset_, width_, height_);

  // TODO just store config_ instead of x_offset_ etc.
  screen_grab::ScreenGrabConfig config;
  config.update_rate = update_rate_;
  config.x_offset = x_offset_;
  config.y_offset = y_offset_;
  config.width = width_;
  config.height = height_;

  server_->updateConfig(config);
}

bool ScreenGrab::spin()
{
  // X resources
  Display* display;
  Screen* screen;
  XImage* xImageSample;
  XColor col;

  // init
  // from vimjay screencap.cpp (https://github.com/lucasw/vimjay)
  {
  display = XOpenDisplay(NULL); // Open first (-best) display
  if (display == NULL) {
    ROS_ERROR_STREAM("bad display");
    return false;
  }

  screen = DefaultScreenOfDisplay(display);
  if (screen == NULL) {
    ROS_ERROR_STREAM("bad screen");
    return false;
  }

  Window wid = DefaultRootWindow( display );
  if ( 0 > wid ) {
    ROS_ERROR_STREAM("Failed to obtain the root windows Id "
        "of the default screen of given display.\n");
    return false;
  }

  XWindowAttributes xwAttr;
  Status ret = XGetWindowAttributes( display, wid, &xwAttr );
  screen_w_ = xwAttr.width;
  screen_h_ = xwAttr.height;
  }

  // get initial values from parameter server, override
  // dr cfg defaults
  getPrivateNodeHandle().getParam("update_rate", update_rate_);
  getPrivateNodeHandle().getParam("x_offset", x_offset_);
  getPrivateNodeHandle().getParam("y_offset", y_offset_);
  getPrivateNodeHandle().getParam("width", width_);
  getPrivateNodeHandle().getParam("height", height_);
  checkRoi(x_offset_, y_offset_, width_, height_);
  loop_rate_ = ros::Rate(update_rate_);

  screen_grab::ScreenGrabConfig config;
  config.update_rate = update_rate_;
  config.x_offset = x_offset_;
  config.y_offset = y_offset_;
  config.width = width_;
  config.height = height_;

  updateConfig();
 
  while (ros::ok()) 
  {
    sensor_msgs::ImagePtr im(new sensor_msgs::Image);
    
    // grab the image
    {
      xImageSample = XGetImage(display, DefaultRootWindow(display),
          x_offset_, y_offset_, width_, height_, AllPlanes, ZPixmap);

      // Check for bad null pointers
      if (xImageSample == NULL) {
        ROS_ERROR_STREAM("Error taking screenshot! "
            << ", " << x_offset_ << " " << y_offset_ 
            << ", " << width_ << " " << height_
            << ", " << screen_w_ << " " << screen_h_
            );
        continue;
      }
      
      // convert to Image format
      XImage2RosImage(*xImageSample, *display, *screen, im);
      
      XDestroyImage(xImageSample);

    }
    
    screen_pub_.publish(im);

    ros::spinOnce();
    loop_rate_.sleep();
  }

  return true;
}

} // screen_grab

#if 0
int main(int argc, char **argv)
{
  ros::init(argc, argv, "screen_grab");

  ScreenGrab screen_grab;
  screen_grab.spin();
}
#endif

