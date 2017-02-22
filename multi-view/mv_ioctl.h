
#define MV_IOCTL_MAGIC 'T'

//flags for controlling the shutdow views iotcl command
#define UNLOCK     0
#define NO_UNLOCK  1


#define IOCTL_SETUP_PID _IOW(MV_IOCTL_MAGIC, 2, unsigned long ) 
#define IOCTL_SHUTDOWN_PID _IOW(MV_IOCTL_MAGIC, 3, unsigned long ) 
#define IOCTL_SHUTDOWN_ACK _IOW(MV_IOCTL_MAGIC, 4, unsigned long ) 
#define IOCTL_SHUTDOWN_VIEWS _IOW(MV_IOCTL_MAGIC, 5, unsigned long ) 
#define IOCTL_CHECKPOINT_PID _IOW(MV_IOCTL_MAGIC, 6, unsigned long ) 

