/*void handle_udp_data(...)
{
	//if suppose no_of_hops are limited to 10
	if(data[0] == 204)
	{
		uint8 ans[16];
		ans[0] = data[0];
		uint8 no_of_hops = data[1]+1;
		uint8 j = 4;
		//uint8 last_hop = 2*data[1];
		//ans[4] = last_hop;
		for(uint8 i = 2*data[1];i>=2;i=i-2)
		{
			ans[j] = data[i];
			j++;
		}
		//uint8 initial_energy = 3;
		float avg_energ = float(data[2*data[1]+1]) + float(data[2*data[1]+2])/100;
		float min_energ = float(data[2*data[1]+3]) + float(data[2*data[1]+4])/100;
		float delta_tk = 1/(3.0 - ((min_energ - no_of_hops)/(avg_energ - no_of_hops)));
		uint8 int_tk = delta_tk;
		uint8 deci_tk = (delta_tk - int_tk)*100;
		ans[2] = int_tk;
		ans[3] = deci_tk;
	}
	else if(data[0] == 205)
	{
		os_.debug("data is %s",data)
	}
}*/
#include <isense/config.h>

#include <isense/application.h>
#include <isense/os.h>
#include <isense/task.h>
#include <isense/timeout_handler.h>
#include <isense/isense.h>
#include <isense/uart.h>
#include <isense/dispatcher.h>
#include <isense/time.h>
#include <isense/util/util.h>
#include <isense/modules/core_module/core_module.h>
#include <isense/protocols/routing/neighborhood_monitor.h>

#include <isense/protocols/ip/version6/icmpv6.h>
#include <isense/protocols/ip/udp.h>
#include <isense/protocols/ip/tcp.h>
#include <isense/protocols/ip/version6/ipv6_address.h>
#include <isense/protocols/ip/version6/new_dymo.h>
#include <isense/protocols/ip/version6/sixlowpan_network_interface.h>
#include <isense/protocols/ip/version6/nd.h>
//#include <math.h>
#include <isense/protocols/routing/flooding.h>
#include <isense/protocols/time_sync/confirm_time.h>
//#include <isense/modules/ethernet_module/enc28j60.h>


#include <isense/uart.h>
//----------------------------------------------------------------------------

using namespace isense;
using namespace ip_stack;


enum userdata{
	UD_SEND_PING = 3,
	UD_CLOSE = 4,
	USERDATA_PRINT_LISTS,
	WAIT_FOR_F_ND,
	UD_SEND_ENERGY_R,
	WAIT_FOR_TIM_SYNC,
	SEND_F_ANTS
};


class UG_IPv6RouterHostDemo :
	public Application,
	public Task,
	public UdpDataHandler,
	public ICMPv6EchoReplyHandler,
	public UartPacketHandler,
	public TimeoutHandler
{
public:
	/*
	 * Constructor
	 */
	UG_IPv6RouterHostDemo(isense::Os& os);
	/*
	 * Destructor
	 */
	virtual ~UG_IPv6RouterHostDemo() ;
	/*
	 * Boot method inherited from Application class
	 */
	virtual void boot (void) ;
	/*
	 * Inherited from Task
	 */
	virtual void execute( void* userdata ) ;
	/*
	 * Inherited from UdpDataHandler
	 */
	void handle_udp_data( IpAddress* remote_host, uint16 remote_port, uint16 local_port, uint16 len, const uint8* data);
	/*
	 * Inherited from ICMPv4EchoReplyHandler
	 */
	void handle_echo_reply(IPv6Address & address, uint16 len, uint8 * data, uint16 identifier, uint16 sequence_number, uint8 hc);


	/*
	 * Method for handling UART packets
	 */
	void handle_uart_packet( uint8 type, uint8* buf, uint8 length );
	/*
	 * Method for printing information about interfaces and assigned adresses
	 */
	void print_lists( uint8 interface_mask , uint8 list_mask );
	virtual void timeout(void* userdata);
	/*
	 * Method for handling timeout
	 */
	int next_neigh();

private:
	/*
	 * Lots of pointers
	 */
	IPv6* ip_;
	Udp* udp_;
	ICMPv6* icmp_;
	SixlowpanNetworkInterface * ip_if0_;
	UdpSocket* udpSocket_;
	TimeSync* ts_;
	CoreModule* cm_;
	float alpha, beta, rho;
	float phermone[4];
    float energy[4];
    float hueristic[4];
    uint8 f_ND;
    Flooding* f_;
    NeighborhoodMonitor::neighbor *N;
    NeighborhoodMonitor* n;
    uint8 check;

};
UG_IPv6RouterHostDemo::
	UG_IPv6RouterHostDemo(isense::Os& os) :
		isense::Application(os),f_ND(0),check(0)
	{
	}

UG_IPv6RouterHostDemo::
	~UG_IPv6RouterHostDemo()
	{
	}
//Ant identifier is the identifier of that node

void
	UG_IPv6RouterHostDemo::
	boot(void)
	{
		os_.allow_sleep(false);
		os_.allow_doze(false);
		#ifdef ISENSE_ENABLE_IPV6_ROUTER
		os_.debug("Booting IPv6 Sensor demo application (Router)");
		#else
		os_.debug("Booting IPv6 Sensor demo application (Host)");
		#endif
		/*
		 * Get some pointer for covenience
		 */
		f_ = new Flooding(os());
		n = new NeighborhoodMonitor(os());
		ip_ = new IPv6( os_ );
		udp_ = &ip_->udp();
		icmp_ = &ip_->icmp();
		cm_ = new CoreModule(os());
		if (cm_ != NULL)
				{
				}
		char address[40];

		/*
		 * Create SixlowpanNetworkInterface and add it to the IPv6 layer
		 */
		ts_ = new TimeSync(os());
		//ts_->request_time_sync(ISENSE_RADIO_BROADCAST_ADDR);
		ts_->launch_time_sync();
		ip_if0_ = new SixlowpanNetworkInterface( os_, *ip_, os_.radio(), 8, false );
		os_.debug("Deb2");

		uint8 if0_index = ip_->add_interface( ip_if0_ );
		//IPv6Address def_router((uint8[]){0x20,0x16,0x0d, 0xb8, 0,0,0x0c,0xa1, 0x00, 0x15, 0x8d, 0x00, 0x00, 0x14, 0x8F, 0x3F});
		//ip_->add_default_router(def_router,0,0xffffffff,0);

		//IPv6Address its((uint8[]){0x20,0x16,0x0d, 0xb8, 0,0,0x0c,0xa1, 0x00, 0x15, 0x8d, 0x00, 0x00, 0x14, 0x8F, 0x39});
		//ip_if0_->add_address(its,0xffffffff,0xffffffff,false);
		os_.debug("Global unicast address of if0 %s", (ip_if0_->get_global_unicast_ip_address())->to_string( address, 40 ));
		#ifdef ISENSE_ENABLE_IPV6_ROUTER
		/*
		 * Set IPv6 prefix of lowpan network. You should alter this to a prefix routed to this device
		 */
		ip_if0_->enable_router( true );
		/*
		 * Add route over protocol
		 */
		IpRouting* dymo = new Dymo( *ip_, if0_index, 20 );
		ip_->add_routing(dymo);
		#endif
		/*
		 * Create UDP socket to listen to
		 */

		 udpSocket_ = udp_->listen(8080, this);

		/*
		 * Wait some time before emission of a Echo Request
		 */
		os_.add_timeout_at( Time(60,0), this, (void*) WAIT_FOR_F_ND );

		os_.uart(0).set_packet_handler( Uart::MESSAGE_TYPE_CUSTOM_IN_1 ,this );
		os_.uart(0).enable_interrupt( true );
 	}
void
	UG_IPv6RouterHostDemo::
	execute( void* userdata )
	{
		if(userdata == (void*)WAIT_FOR_F_ND)
		{
			f_ND++;
			if(f_ND<=3)
			{
				uint8 buf[2];
				buf[0] = 200;
				buf[1] = 2;
				if(f_->send(2,buf))
				{
					os_.debug("sending data through flooding");
				}
				os_.add_timeout_in(Time(10,0),this,(void*)WAIT_FOR_F_ND);
			}
			else
			{
				if(N)
					delete N;
				NeighborhoodMonitor::neighbor_property pro = NeighborhoodMonitor::neighbor_property(0x05);
				N = n -> get_neighbors( pro);
				for(int i=0;i<3;i++)
						if(N[i].addr != 0xffff)
							os_.debug("neighbor %d: %x%x  signal strength: %d",i,N[i].addr,N[i].value);
				n->reset();
				os_.add_timeout_in(Time(7,0), this,(void*)WAIT_FOR_F_ND);
				f_ND=0;
			}
		}
		/*if( (userdata == (void*)UD_SEND_ENERGY_R) || (userdata == (void*)SEND_F_ANTS)){
			/*
			 * Create IPv6Address object.
			 * Put the IPv6 link-local address of the radio interface of your Ethernet Gateway here
			 */
			 /*f_ND++;
			 if(f_ND<=2)
			 {
				for(int i =0;i<4;i++)
				{

				//uint8 buf[10];
				uint32 firs_six = (N[i].addr)%(16777216ULL);
				uint8 C1 = (firs_six)%256;
				uint8 B1 = (firs_six>>8)%256;
				uint8 A1 = (firs_six>>16)%256;
			//IPv6Address target( (uint8[]){0x20,0x16,0x0d, 0xb8, 0,0,0x0c,0xa1, 0x00, 0x15, 0x8d, 0x00, 0x00, 0x14, 0x8F, 0x3F});

			//uint8 buf[100];
				uint8 A[16] = {0xfe,0x80,0x00, 0x00, 0,0,0x00,0x00, 0x02, 0x15, 0x8d, 0x00, 0x00};
				A[13]=A1;A[14]=B1;A[15] = C1;
				IPv6Address target(A);
				target.interface_id_ = 0;
				IpAddress remote_host = target;
				uint16 length;
				uint8 answer[26];
				if(userdata == (void*)UD_SEND_ENERGY_R)
				{
				uint16 voltage = cm_->supply_voltage();
				answer[0] = 200;
				answer[1] = voltage/100;
				answer[2] = (voltage/100.0 - answer[0])*100;
				length = 3;
				}
				if(userdata == (void*)SEND_F_ANTS)
				{

					answer[0] = 201;
					uint32 this_id =  (os_.id())%(16777216ULL);
					answer[3] = this_id%256;
					answer[2] = (this_id>>8)%256;
					answer[1] = (this_id>>16)%256;
					length = 26;
				}
			/*if(hur==true)
			{
				os_.debug("hippip hurray!");
			}
			else
			{
				os_.debug("Shit!");
			}*/
			//char answer[1280];
			//uint8 answer[3];
			//uint16 length = snprintf( answer, 1280,"UDP response from(6LoWPAN Router): " );
			//answer[length] = '\0';
			//length++;
			//uint16 length = 3;
			//answer[0] = 200;
			//answer[1] = 198;
		//	answer[2] = 24;
				//Buffer ans = {length, (uint8*)answer};
			/*
			 * Set ICMP echo reply handler and
			 * emit ping message
	        */

				/*icmp_->set_echo_reply_handler( this );

				udpSocket_->send( ans, MOD_COPY, &remote_host, 8080, NULL);

		    	}
		    os_.add_timeout_in(Time(6,0),this,(void*)UD_SEND_ENERGY_R);
			}
			else
			{
				for(int i = 0;i<4;i++)
				{
					phermone[i] = 0.5;
				}
				f_ND = 0;
				os_.add_task(this,(void*)SEND_F_ANTS);
			}
		}*/
			//icmp_->ping(target, 10, buf);
			/*
			 * Some debug output and some clean up
			 */
			//char saddr[40];
			//os_.debug("Echo Request sent to %s", target.to_string(saddr));

		else if ( userdata == (void*) USERDATA_PRINT_LISTS ) {
				os_.debug( "Free Mem: %d", mem->mem_free() );
				print_lists( 0xff , 0xff );
			}
	}

	//else if(buf[0] == 205)
	//{
		//forward_data;
//	}

void
	UG_IPv6RouterHostDemo::
	handle_udp_data( IpAddress* remote_host, uint16 remote_port, uint16 local_port, uint16 len, const uint8* data){

	/*if(data[0] == 200)
	{
		uint8* rem = remote_host->get_array();
		for(int i = 0;i<4;i++)
		{
			if(((N[i].addr)%256 == rem[15]) && ((((N[i].addr)%65536)>>8) == rem[14]) && ((((N[i].addr)%(16777216ULL))>>16) == rem[13]))
			{
				energy[i] = data[0]/10.0 + data[1]/1000.0;
			}
		}
	}
	else if(data[0] == 201)
	{

	}
	else if(data[0] == 202)
	{
		energy[2] = data[0];
	}
	else if(data[0] == 203)
	{
		energy[3] = data[0];
	}
	else if(data[0] == 204)
	{
		//forward_ant
		//write code for switching on led
		Time tim = os_.time();
		ISENSE_RADIO_ADDR_TYPE iden = os_.id();
		uint8 hop_id = iden%256;
		bool dele = FALSE;
		for(uint8 i = 2*data[1];i>=2;i=i-2)
		{

			if(data[i] == hop_id)
			{
				if(i/2<=data[1]/2)
				{
					dele = TRUE;
				}
			}
		}
		if(data[2*data[1]+5]-tim.sec_ >20)
		{
			dele = TRUE;
		}
		if(dele != TRUE)
		{
			int next = next_neigh();
		}
    }*/
    if((data[0] == 202))
	{
		//Time tim = os_.time();
		bool dele = FALSE;

		/*if(tim.sec_ - data[4] > 20)
		{
			dele = TRUE;
		}*/
		if(data[4]+1>5)
		{
			dele = TRUE;
		}
		if(dele != TRUE)
		{
			uint8 answer[35];
			answer[0] = 203;
			os_.debug("new source %x",data[1]);
			os_.debug("%d", data[4]);
			answer[1] = data[1];
			//answer[2] = data[2];
			//answer[3] = data[3];
			int16 no_o_ho = data[4];
		 	answer[4] = data[4]+1;
		 	answer[5] = 0;
		 	uint8 j = 6;
		 	for(uint8 i = 4+data[4];no_o_ho>0;i--)
		 	{
		 			answer[j] = data[i];
		 			//answer[j+1] = data[i+1];
		 			//answer[j+2] = data[i+2];
		 			os_.debug("%d %x",j,answer[j]);
		 			no_o_ho--;
		 			j ++;
			}

		//uint8 last_hop = 2*data[1];
		//ans[4] = last_hop;
		/*for(uint8 i = 2*data[1];i>=2;i=i-2)
		{
			ans[j] = data[i];
			j++;
		}*/
		//uint8 initial_energy = 3;
		float avg_energ = data[2]/10.0 + data[3]/1000.0;
		//float min_energ = float(data[2*data[1]+3]) + float(data[2*data[1]+4])/100;
		//float delta_tk = 1/(3.0 - ((min_energ - no_of_hops)/(avg_energ - no_of_hops)));
		float delta_tk = (1/27.0)*(10-answer[4])*avg_energ;
		uint8 int_tk = delta_tk*10;
		uint8 deci_tk = (delta_tk*10 - int_tk)*100;
		answer[2] = int_tk;
		answer[3] = deci_tk;
		uint8 B[16] = {0xfe,0x80,0x00, 0x00, 0,0,0x00,0x00, 0x02, 0x15, 0x8d, 0x00, 0x00, 0x14, 0x8F};
		//B[13] = answer[8];B[14] = answer[9];
		B[15] = answer[6];
		IPv6Address target(B);
		target.interface_id_ = 0;
		IpAddress remote_host = target;
		uint16 length = 16;
		Buffer ans = {length, (uint8*)answer};
		udpSocket_->send( ans, MOD_COPY, &remote_host, 8080, NULL);
	}
	else if(data[0] == 205)
	{
		os_.debug("data is %s",data);
	}
	}
}
/*int
	UG_IPv6RouterHostDemo::next_neigh()
{
	int next_neigh(0);
	float denom(0.0);
	float heur_denom(0.0);
	float prob[4];
	heur_denom = energy[0] + energy[1] + enrgy[2] + energy[3];
	for(int i=0;i<4;i++)
	{
		heuristic[i] = energy[i]/heur_denom;
		prob[i] = phermone[i]*heuristic[i]*heuristic[i];
		denom += prob[i];
	}
	prob[0] /= denom;
	prob[1] /= denom;
	prob[2] /= denom;
	prob[3] /= denom;
	int max_prob = max(prob);
	for(int i =0;i<4;i++)
	{
		if(prob[i] == max_prob)
		{
			next_neigh = i;
			break;
		}
	}
	return i;
}*/
void
	UG_IPv6RouterHostDemo::
	handle_echo_reply(IPv6Address & address, uint16 len, uint8 * data, uint16 identifier, uint16 sequence_number, uint8 hc){
		uint8 interface = ((IPv6Address*)&address)->interface_id_;
		IPv6NetworkInterface* ifx = ip_->get_interface( interface );
		char source[40];
		char target[40];
		os_.debug("Echo Reply received from %s to %s, length %u, identifier %u, sequence_number %u, hop_count %u", address.to_string(source), (ifx->get_link_local_unicast_ip_address())->to_string( target, 40), len, identifier, sequence_number, hc);

		char answer[6] = "hello";
		answer[5]='\0';
				uint16 length = 6;
				Buffer ans = {length, (uint8*)answer};
				//uint8* source1 = address.get_array();
				IpAddress Ip = address;
				//Ip->set_array(source1);

				udpSocket_->send( ans, MOD_COPY, &Ip,8080,NULL );


		/*
		//IPv6Address target( (uint8[]){ 0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x02,0x15,0x8d,0x00,0x00,0x14,0x8f,0x5d} );
							//target.interface_id_ = 0;
				//IpAddress remote_host = target;
				IpAddress remote_host = address;

					uint16 length = 40;
						char answer[1280] =  "HI PEROPLE ... SENDING DATA ..! WOW !!\0";
						Buffer ans = {length, (uint8*)answer};
						if(udpSocket_->send( ans, MOD_DELETE_ALLOWED, &remote_host, 8080, NULL))
							os_.debug("data sent!!");
		 */
	}
void
UG_IPv6RouterHostDemo::
	handle_uart_packet( uint8 type, uint8* buf, uint8 length )
{
 if ( type == Uart::MESSAGE_TYPE_CUSTOM_IN_1 ) {
		os_.add_task( this , (void*) USERDATA_PRINT_LISTS);
	}
}

void
UG_IPv6RouterHostDemo::
print_lists( uint8 interface_mask , uint8 list_mask )
{
	if ( ip_ != NULL ) {
		for (uint8 i = 0; i < 8; i++) {
			if (interface_mask & 0x01) {
				IPv6NetworkInterface* interface = ip_->get_interface(i);
				if ( interface != NULL ) {
					char c[100];
					os_.debug("%s" , interface->to_string(c,99));
					if ( list_mask & 0x01 ) {
						interface->print_address_list();
					}
					if ( list_mask & 0x02 ) {
						interface->print_prefix_list();
					}
					if ( list_mask & 0x04 ) {
						interface->print_default_router_list();
					}
					if ( list_mask & 0x08 ) {
						interface->print_neighbor_cache();
					}
					os_.debug("");
				}
			}
			interface_mask >>= 1;
		}
		os_.debug("");

	}
}

void UG_IPv6RouterHostDemo::timeout(void* userdata)
{
	if(userdata == (void*)WAIT_FOR_TIM_SYNC)
	{
		os_.add_timeout_at(Time(60,0),this,(void*)WAIT_FOR_F_ND);
	}
	if(userdata == (void*)WAIT_FOR_F_ND)
	{
		os_.add_task(this,(void*)WAIT_FOR_F_ND);
		//os_.add_timeout_in(Time(0,0),this,(void*)WAIT_FOR_F_ND);
	}
	if((userdata == (void*)UD_SEND_ENERGY_R) )
	{
		os_.add_task(this,(void*)UD_SEND_ENERGY_R);
	}
	//os_.add_timeout_in(Time(2,0),this,NULL);
}
isense::Application* application_factory(isense::Os& os)
{
	return new UG_IPv6RouterHostDemo(os);
}
