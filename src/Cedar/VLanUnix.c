// SoftEther VPN Source Code - Developer Edition Master Branch
// Cedar Communication Module


// VLanUnix.c
// Virtual device driver library for UNIX

#ifdef UNIX

#include "VLanUnix.h"

#include "Connection.h"
#include "Session.h"

#include "Mayaqua/FileIO.h"
#include "Mayaqua/Mayaqua.h"
#include "Mayaqua/Memory.h"
#include "Mayaqua/Str.h"
#include "Mayaqua/TunTap.h"

#ifdef UNIX_BSD
// For "sockaddr" in <net/if_arp.h>
#include <sys/socket.h>
#endif

#include <errno.h>
#include <fcntl.h> 
#include <net/if_arp.h>
#include <net/if.h>
#include <sys/ioctl.h>

#if defined(UNIX_OPENBSD) || defined(UNIX_SOLARIS)
#include <netinet/if_ether.h>
#else
#include <net/ethernet.h>
#endif

static LIST *unix_vlan = NULL;

#ifndef NO_VLAN

// Get the PACKET_ADAPTER
PACKET_ADAPTER *VLanGetPacketAdapter()
{
	PACKET_ADAPTER *pa;

	pa = NewPacketAdapter(VLanPaInit, VLanPaGetCancel,
		VLanPaGetNextPacket, VLanPaPutPacket, VLanPaFree);
	if (pa == NULL)
	{
		return NULL;
	}

	return pa;
}

// PA initialization
bool VLanPaInit(SESSION *s)
{
	VLAN *v;
	// Validate arguments
	if (s == NULL)
	{
		return false;
	}

	// Connect to the driver
	v = NewVLan(s->ClientOption->DeviceName, NULL);
	if (v == NULL)
	{
		// Failure
		return false;
	}

	s->PacketAdapter->Param = v;

	// Routing table tracking start
	if (s->ClientModeAndUseVLan)
	{
		RouteTrackingStart(s);
	}
	
	return true;
}

// Get the cancel object
CANCEL *VLanPaGetCancel(SESSION *s)
{
	VLAN *v;
	// Validate arguments
	if ((s == NULL) || ((v = s->PacketAdapter->Param) == NULL))
	{
		return NULL;
	}

	return VLanGetCancel(v);
}

// Release the packet adapter
void VLanPaFree(SESSION *s)
{
	VLAN *v;
	ROUTE_TRACKING *t;
	// Validate arguments
	if ((s == NULL) || ((v = s->PacketAdapter->Param) == NULL))
	{
		return;
	}

	// End the virtual LAN card
	FreeVLan(v);

	// End the routing table tracking 
	if (s->ClientModeAndUseVLan)
	{
		RouteTrackingStop(s, t);
	}

	s->PacketAdapter->Param = NULL;
}

// Write a packet
bool VLanPaPutPacket(SESSION *s, void *data, UINT size)
{
	VLAN *v;
	// Validate arguments
	if ((s == NULL) || ((v = s->PacketAdapter->Param) == NULL))
	{
		return false;
	}

	return VLanPutPacket(v, data, size);
}

// Get the next packet
UINT VLanPaGetNextPacket(SESSION *s, void **data)
{
	VLAN *v;
	UINT size;
	// Validate arguments
	if (data == NULL || (s == NULL) || ((v = s->PacketAdapter->Param) == NULL))
	{
		return INFINITE;
	}

	RouteTrackingMain(s);

	if (VLanGetNextPacket(v, data, &size) == false)
	{
		return INFINITE;
	}

	return size;
}

// Routing table tracking main
void RouteTrackingMain(SESSION *s)
{
	ROUTE_TRACKING *t;
	UINT64 now;
	ROUTE_TABLE *table;
	ROUTE_ENTRY *rs;
	bool changed = false;
	bool check = false;
	bool any_modified = false;
	// Validate arguments
	if (s == NULL)
	{
		return;
	}
	if (s->ClientModeAndUseVLan == false)
	{
		return;
	}

	// Get the state
	t = ((VLAN *)s->PacketAdapter->Param)->RouteState;
	if (t == NULL)
	{
		return;
	}

	// Current time
	PROBE_STR("RouteTrackingMain 1");
	now = Tick64();

	if (t->RouteChange != NULL)
	{
		if (t->NextRouteChangeCheckTime == 0 ||
			t->NextRouteChangeCheckTime <= now)
		{
			t->NextRouteChangeCheckTime = now + 1000ULL;

			check = IsRouteChanged(t->RouteChange);

			if (check)
			{
				Debug("*** Routing Table Changed ***\n");
				t->NextTrackingTime = 0;
			}
		}
	}
	if (t->NextTrackingTime != 0 && t->NextTrackingTime > now)
	{
		if (s->UseUdpAcceleration && s->UdpAccel != NULL && s->UdpAccel->NatT_IP_Changed)
		{
			// Check always if the IP address of the NAT-T server has changed
		}
		else
		{
			PROBE_STR("RouteTrackingMain 2");
			return;
		}
	}
	PROBE_STR("RouteTrackingMain 3");

	if (s->UseUdpAcceleration && s->UdpAccel != NULL)
	{
		IP nat_t_ip;

		s->UdpAccel->NatT_IP_Changed = false;

		Zero(&nat_t_ip, sizeof(nat_t_ip));

		Lock(s->UdpAccel->NatT_Lock);
		{
			Copy(&nat_t_ip, &s->UdpAccel->NatT_IP, sizeof(IP));
		}
		Unlock(s->UdpAccel->NatT_Lock);

		// Add a route to the NAT-T server
		if (IsZeroIp(&nat_t_ip) == false)
		{
			if (t->RouteToNatTServer == NULL)
			{
				if (t->RouteToEight != NULL)
				{
					ROUTE_ENTRY *e = Clone(t->RouteToEight, sizeof(ROUTE_ENTRY));
					char ip_str[64];
					char ip_str2[64];

					Copy(&e->DestIP, &nat_t_ip, sizeof(IP));

					IPToStr(ip_str, sizeof(ip_str), &e->DestIP);
					IPToStr(ip_str2, sizeof(ip_str2), &e->GatewayIP);

					t->RouteToNatTServer = e;

					if (AddRouteEntry(t->RouteToNatTServer))
					{
						Debug("Adding Static Route to %s via %s metric %u: ok.\n", ip_str, ip_str2, e->Metric);
					}
					else
					{
						FreeRouteEntry(t->RouteToNatTServer);
						t->RouteToNatTServer = NULL;
					}
				}
			}
		}
	}

	// Get the current routing table
	table = GetRouteTable();
	rs = t->RouteToServer;
	if (table != NULL)
	{
		UINT i;
		bool route_to_server_erased = true;
		bool is_vlan_want_to_be_default_gateway_v4 = false;
		bool is_vlan_want_to_be_default_gateway_v6 = false;
		UINT vlan_default_gateway_metric_v4 = 0;
		UINT vlan_default_gateway_metric_v6 = 0;
		UINT other_if_default_gateway_metric_min_v4 = INFINITE;
		UINT other_if_default_gateway_metric_min_v6 = INFINITE;

		// Get whether the routing table have been changed
		if (t->LastRoutingTableHash != table->HashedValue)
		{
			t->LastRoutingTableHash = table->HashedValue;
			changed = true;
		}

		//DebugPrintRouteTable(table);

		// Scan the routing table
		for (i = 0;i < table->NumEntry;i++)
		{
			ROUTE_ENTRY *e = table->Entry[i];

			if (rs != NULL)
			{
				if (CmpIpAddr(&e->DestIP, &rs->DestIP) == 0 &&
					CmpIpAddr(&e->DestMask, &rs->DestMask) == 0
//					&& CmpIpAddr(&e->GatewayIP, &rs->GatewayIP) == 0
//					&& e->InterfaceID == rs->InterfaceID &&
//					e->LocalRouting == rs->LocalRouting &&
//					e->Metric == rs->Metric
					)
				{
					// Routing entry to the server that added at the time of connection is found
					route_to_server_erased = false;
				}
			}

			// Search for the default gateway
			if (IsZeroIP(&e->DestIP) && IsZeroIP(&e->DestMask))
			{
				Debug("e->InterfaceID = %u, t->VLanInterfaceId = %u\n",
					e->InterfaceID, t->VLanInterfaceId);

				if (e->InterfaceID == t->VLanInterfaceId)
				{
					if (IsIP4(&e->DestIP))
					{
						// The virtual LAN card think that he want to be a default gateway
						is_vlan_want_to_be_default_gateway_v4 = true;
						vlan_default_gateway_metric_v4 = e->Metric;

						// PPP route fix
						if (vlan_default_gateway_metric_v4 >= 2 &&
							t->OldDefaultGatewayMetric == (vlan_default_gateway_metric_v4 - 1))
						{
							// Restore because the PPP server rewrites
							// the routing table selfishly
							DeleteRouteEntry(e);
							e->Metric--;
							AddRouteEntry(e);
							Debug("** Restore metric destroyed by PPP.\n");

							any_modified = true;
						}

						// Keep this entry
						if (t->DefaultGatewayByVLan != NULL)
						{
							// Delete if there is one added last time
							FreeRouteEntry(t->DefaultGatewayByVLan);
						}

						t->DefaultGatewayByVLan = ZeroMalloc(sizeof(ROUTE_ENTRY));
						Copy(t->DefaultGatewayByVLan, e, sizeof(ROUTE_ENTRY));

						t->OldDefaultGatewayMetric = vlan_default_gateway_metric_v4;
					}
					else
					{
						is_vlan_want_to_be_default_gateway_v6 = true;
						vlan_default_gateway_metric_v6 = e->Metric;
					}
				}
				else
				{
					if (IsIP4(&e->DestIP))
					{
						// There are default gateway other than the virtual LAN card
						// Save the metric value of the default gateway
						if (other_if_default_gateway_metric_min_v4 > e->Metric)
						{
							// Ignore the metric value of all PPP connection in the case of Windows Vista
							if (e->PPPConnection == false)
							{
								other_if_default_gateway_metric_min_v4 = e->Metric;
							}
							else
							{
								// a PPP is used to Connect to the network
								// in using Windows Vista
								t->VistaAndUsingPPP = true;
							}
						}
					}
					else
					{
						if (other_if_default_gateway_metric_min_v6 > e->Metric)
						{
							other_if_default_gateway_metric_min_v6 = e->Metric;
						}
					}
				}
			}
		}

		if (t->VistaAndUsingPPP)
		{
			if (t->DefaultGatewayByVLan != NULL)
			{
				if (is_vlan_want_to_be_default_gateway_v4)
				{
					if (t->VistaOldDefaultGatewayByVLan == NULL || Cmp(t->VistaOldDefaultGatewayByVLan, t->DefaultGatewayByVLan, sizeof(ROUTE_ENTRY)) != 0)
					{
						ROUTE_ENTRY *e;
						// Add the route of 0.0.0.0/1 and 128.0.0.0/1
						// to the system if the virtual LAN card should be
						// the default gateway in the case of the connection
						// using PPP in Windows Vista

						if (t->VistaOldDefaultGatewayByVLan != NULL)
						{
							FreeRouteEntry(t->VistaOldDefaultGatewayByVLan);
						}

						if (t->VistaDefaultGateway1 != NULL)
						{
							DeleteRouteEntry(t->VistaDefaultGateway1);
							FreeRouteEntry(t->VistaDefaultGateway1);

							DeleteRouteEntry(t->VistaDefaultGateway2);
							FreeRouteEntry(t->VistaDefaultGateway2);
						}

						t->VistaOldDefaultGatewayByVLan = Clone(t->DefaultGatewayByVLan, sizeof(ROUTE_ENTRY));

						e = Clone(t->DefaultGatewayByVLan, sizeof(ROUTE_ENTRY));
						SetIP(&e->DestIP, 0, 0, 0, 0);
						SetIP(&e->DestMask, 128, 0, 0, 0);
						t->VistaDefaultGateway1 = e;

						e = Clone(t->DefaultGatewayByVLan, sizeof(ROUTE_ENTRY));
						SetIP(&e->DestIP, 128, 0, 0, 0);
						SetIP(&e->DestMask, 128, 0, 0, 0);
						t->VistaDefaultGateway2 = e;

						AddRouteEntry(t->VistaDefaultGateway1);
						AddRouteEntry(t->VistaDefaultGateway2);

						Debug("Vista PPP Fix Route Table Added.\n");

						any_modified = true;
					}
				}
				else
				{
					if (t->VistaOldDefaultGatewayByVLan != NULL)
					{
						FreeRouteEntry(t->VistaOldDefaultGatewayByVLan);
						t->VistaOldDefaultGatewayByVLan = NULL;
					}

					if (t->VistaDefaultGateway1 != NULL)
					{
						Debug("Vista PPP Fix Route Table Deleted.\n");
						DeleteRouteEntry(t->VistaDefaultGateway1);
						FreeRouteEntry(t->VistaDefaultGateway1);

						DeleteRouteEntry(t->VistaDefaultGateway2);
						FreeRouteEntry(t->VistaDefaultGateway2);

						any_modified = true;

						t->VistaDefaultGateway1 = t->VistaDefaultGateway2 = NULL;
					}
				}
			}
		}

		// If the virtual LAN card want to be the default gateway and
		// there is no LAN card with smaller metric of 0.0.0.0/0 than
		// the virtual LAN card, delete other default gateway entries
		// to elect the virtual LAN card as the default gateway
//		Debug("is_vlan_want_to_be_default_gateway = %u, rs = %u, route_to_server_erased = %u, other_if_default_gateway_metric_min = %u, vlan_default_gateway_metric = %u\n",
//			is_vlan_want_to_be_default_gateway, rs, route_to_server_erased, other_if_default_gateway_metric_min, vlan_default_gateway_metric);
		if ((is_vlan_want_to_be_default_gateway_v4 && other_if_default_gateway_metric_min_v4 >= vlan_default_gateway_metric_v4 ||
			is_vlan_want_to_be_default_gateway_v6 && other_if_default_gateway_metric_min_v6 >= vlan_default_gateway_metric_v6)
			&& rs != NULL && route_to_server_erased == false)
		{
			// Scan the routing table again
			for (i = 0;i < table->NumEntry;i++)
			{
				ROUTE_ENTRY *e = table->Entry[i];

				if (e->InterfaceID != t->VLanInterfaceId)
				{
					if (IsZeroIP(&e->DestIP) && IsZeroIP(&e->DestMask))
					{
						char str[64];
						// Default gateway is found
						ROUTE_ENTRY *r = ZeroMalloc(sizeof(ROUTE_ENTRY));

						Copy(r, e, sizeof(ROUTE_ENTRY));

						// Put in the queue
						InsertQueue(t->DeletedDefaultGateway, r);

						// Delete this gateway entry once
						DeleteRouteEntry(e);

						IPToStr(str, sizeof(str), &e->GatewayIP);
						Debug("Default Gateway %s Deleted.\n", str);

						any_modified = true;
					}
				}
			}
		}

		if (rs != NULL && route_to_server_erased)
		{
			// Physical entry to the server has disappeared
			Debug("Route to Server entry ERASED !!!\n");

			// Forced disconnection (reconnection enabled)
			s->RetryFlag = true;
			s->Halt = true;
		}

		// Release the routing table
		FreeRouteTable(table);
	}

	// Set the time to perform the next track
	if (t->NextTrackingTimeAdd == 0 || changed)
	{
		t->NextTrackingTimeAdd = TRACKING_INTERVAL_INITIAL;
	}
	else
	{
		UINT64 max_value = TRACKING_INTERVAL_MAX;
		if (t->RouteChange != NULL)
		{
			max_value = TRACKING_INTERVAL_MAX_RC;
		}

		t->NextTrackingTimeAdd += TRACKING_INTERVAL_ADD;

		if (t->NextTrackingTimeAdd >= max_value)
		{
			t->NextTrackingTimeAdd = max_value;
		}
	}
	//Debug("t->NextTrackingTimeAdd = %I64u\n", t->NextTrackingTimeAdd);
	t->NextTrackingTime = now + t->NextTrackingTimeAdd;

	if (any_modified)
	{
		// Clear the DNS cache
		Win32FlushDnsCache();
	}
}

// Start tracking of the routing table
void RouteTrackingStart(SESSION *s)
{
	VLAN *v;
	ROUTE_TRACKING *t;
	UINT if_id = 0;
	ROUTE_ENTRY *e;
	ROUTE_ENTRY *dns = NULL;
	ROUTE_ENTRY *route_to_real_server_global = NULL;
	char tmp[64];
	UINT exclude_if_id = 0;
	bool already_exists = false;
	bool already_exists_by_other_account = false;
	IP eight;
	// Validate arguments
	if (s == NULL)
	{
		return;
	}

	v = (VLAN *)s->PacketAdapter->Param;
	if (v->RouteState != NULL)
	{
		return;
	}

	// Get the interface ID of the virtual LAN card
	if_id = GetInstanceId(v->InstanceName);
	Debug("[InstanceId of %s] = 0x%x\n", v->InstanceName, if_id);

	// The routing table by the virtual LAN card body should be
	// excluded explicitly in Windows Vista
	exclude_if_id = if_id;

	// Get the route to the server
	e = GetBestRouteEntryEx(&s->ServerIP, exclude_if_id);
	if (e == NULL)
	{
		// Acquisition failure
		Debug("Failed to get GetBestRouteEntry().\n");
		return;
	}
	IPToStr(tmp, sizeof(tmp), &e->GatewayIP);
	Debug("GetBestRouteEntry() Succeed. [Gateway: %s]\n", tmp);

	// Add a route
	if (AddRouteEntryEx(e, &already_exists) == false)
	{
		FreeRouteEntry(e);
		e = NULL;
	}
	Debug("already_exists: %u\n", already_exists);

	if (already_exists)
	{
		if (s->Cedar->Client != NULL && s->Account != NULL)
		{
			UINT i;
			ACCOUNT *a;
			for (i = 0;i < LIST_NUM(s->Cedar->Client->AccountList);i++)
			{
				a = LIST_DATA(s->Cedar->Client->AccountList, i);
				Lock(a->lock);
				{
					SESSION *sess = a->ClientSession;
					if (sess != NULL && sess != s)
					{
						VLAN *v = sess->PacketAdapter->Param;
						if (v != NULL)
						{
							ROUTE_TRACKING *tr = v->RouteState;
							if (tr != NULL && e != NULL)
							{
								if (Cmp(tr->RouteToServer, e, sizeof(ROUTE_ENTRY)) == 0)
								{
									already_exists_by_other_account = true;
								}
							}
						}
					}
				}
				Unlock(a->lock);
			}
		}

		if (already_exists_by_other_account)
		{
			Debug("already_exists_by_other_account = %u\n", already_exists_by_other_account);
			already_exists = false;
		}
	}

	// Get the routing table to the DNS server
	// (If the DNS server is this PC itself, there's no need to get)
	if (IsZeroIP(&s->DefaultDns) == false)
	{
		if (IsMyIPAddress(&s->DefaultDns) == false)
		{
			dns = GetBestRouteEntryEx(&s->DefaultDns, exclude_if_id);
			if (dns == NULL)
			{
				// Getting failure
				Debug("Failed to get GetBestRouteEntry DNS.\n");
			}
			else
			{
				// Add a route
				if (AddRouteEntry(dns) == false)
				{
					FreeRouteEntry(dns);
					dns = NULL;
				}
			}
		}
	}

	if (s->IsAzureSession && IsZeroIP(&s->AzureRealServerGlobalIp) == false)
	{
		// Add also a static route to the real server in the case of via VPN Azure
		if (IsMyIPAddress(&s->AzureRealServerGlobalIp) == false)
		{
			route_to_real_server_global = GetBestRouteEntryEx(&s->AzureRealServerGlobalIp, exclude_if_id);

			if (route_to_real_server_global != NULL)
			{
				if (AddRouteEntry(route_to_real_server_global) == false)
				{
					FreeRouteEntry(route_to_real_server_global);
					route_to_real_server_global = NULL;
				}
			}
		}
	}

	// Initialize
	if (s->Cedar->Client != NULL && s->Account != NULL)
	{
		Lock(s->Account->lock);
	}

	t = ZeroMalloc(sizeof(ROUTE_TRACKING));
	v->RouteState = t;

	t->RouteToServerAlreadyExists = already_exists;
	t->RouteToServer = e;
	t->RouteToDefaultDns = dns;
	t->RouteToRealServerGlobal = route_to_real_server_global;
	t->VLanInterfaceId = if_id;
	t->NextTrackingTime = 0;
	t->DeletedDefaultGateway = NewQueue();
	t->OldDefaultGatewayMetric = 0x7fffffff;

	if (s->Cedar->Client != NULL && s->Account != NULL)
	{
		Unlock(s->Account->lock);
	}

	// Get the route to 8.8.8.8
	SetIP(&eight, 8, 8, 8, 8);
	t->RouteToEight = GetBestRouteEntryEx(&eight, exclude_if_id);

	// Get the current default DNS server to detect network changes
	GetDefaultDns(&t->OldDnsServer);

	// Get as soon as releasing the IP address in the case of using DHCP
	if (IsNt())
	{
		char tmp[MAX_SIZE];
		MS_ADAPTER *a;

		Format(tmp, sizeof(tmp), VLAN_ADAPTER_NAME_TAG, v->InstanceName);
		a = MsGetAdapter(tmp);

		if (a != NULL)
		{
			if (a->UseDhcp)
			{
				bool ret = Win32ReleaseAddressByGuidEx(a->Guid, 100);
				Debug("*** Win32ReleaseAddressByGuidEx = %u\n", ret);

				ret = Win32RenewAddressByGuidEx(a->Guid, 100);
				Debug("*** Win32RenewAddressByGuidEx = %u\n", ret);
			}

			MsFreeAdapter(a);
		}
	}

	// Clear the DNS cache
	Win32FlushDnsCache();

	// Detect a change in the routing table (for only supported OS)
	t->RouteChange = NewRouteChange();
	Debug("t->RouteChange = 0x%p\n", t->RouteChange);
}

// End the tracking of the routing table
void RouteTrackingStop(SESSION *s, ROUTE_TRACKING *t)
{
	ROUTE_ENTRY *e;
	ROUTE_TABLE *table;
	IP dns_ip;
	bool network_has_changed = false;
	bool do_not_delete_routing_entry = false;
	// Validate arguments
	if (s == NULL || t == NULL)
	{
		return;
	}

	Zero(&dns_ip, sizeof(dns_ip));

	if (t->DefaultGatewayByVLan != NULL)
	{
		FreeRouteEntry(t->DefaultGatewayByVLan);
		t->DefaultGatewayByVLan = NULL;
	}

	if (t->VistaDefaultGateway1 != NULL)
	{
		Debug("Vista PPP Fix Route Table Deleted.\n");
		DeleteRouteEntry(t->VistaDefaultGateway1);
		FreeRouteEntry(t->VistaDefaultGateway1);

		DeleteRouteEntry(t->VistaDefaultGateway2);
		FreeRouteEntry(t->VistaDefaultGateway2);
	}

	// Clear the DNS cache
	Win32FlushDnsCache();

	if (s->Cedar->Client != NULL && s->Account != NULL)
	{
		UINT i;
		ACCOUNT *a;
		for (i = 0;i < LIST_NUM(s->Cedar->Client->AccountList);i++)
		{
			a = LIST_DATA(s->Cedar->Client->AccountList, i);
			Lock(a->lock);
			{
				SESSION *sess = a->ClientSession;
				if (sess != NULL && sess != s)
				{
					VLAN *v = sess->PacketAdapter->Param;
					if (v != NULL)
					{
						ROUTE_TRACKING *tr = v->RouteState;
						if (tr != NULL)
						{
							if (Cmp(tr->RouteToServer, t->RouteToServer, sizeof(ROUTE_ENTRY)) == 0)
							{
								do_not_delete_routing_entry = true;
							}
						}
					}
				}
			}
			Unlock(a->lock);
		}

		Lock(s->Account->lock);
	}

	if (do_not_delete_routing_entry == false)
	{
		// Delete the route that is added firstly
		if (t->RouteToServerAlreadyExists == false)
		{
			DeleteRouteEntry(t->RouteToServer);
		}

		DeleteRouteEntry(t->RouteToDefaultDns);

		DeleteRouteEntry(t->RouteToNatTServer);

		DeleteRouteEntry(t->RouteToRealServerGlobal);
	}

	FreeRouteEntry(t->RouteToDefaultDns);
	FreeRouteEntry(t->RouteToServer);
	FreeRouteEntry(t->RouteToEight);
	FreeRouteEntry(t->RouteToNatTServer);
	FreeRouteEntry(t->RouteToRealServerGlobal);
	t->RouteToDefaultDns = t->RouteToServer = t->RouteToEight =
		t->RouteToNatTServer = t->RouteToRealServerGlobal = NULL;

	if (s->Cedar->Client != NULL && s->Account != NULL)
	{
		Unlock(s->Account->lock);
	}

#if	0
	// Get the current DNS server
	if (GetDefaultDns(&dns_ip))
	{
		if (IPToUINT(&t->OldDnsServer) != 0)
		{
			if (IPToUINT(&t->OldDnsServer) != IPToUINT(&dns_ip))
			{
				char s1[MAX_SIZE], s2[MAX_SIZE];
				network_has_changed = true;
				IPToStr(s1, sizeof(s1), &t->OldDnsServer);
				IPToStr(s2, sizeof(s2), &dns_ip);
				Debug("Old Dns: %s, New Dns: %s\n",
					s1, s2);
			}
		}
	}

	if (network_has_changed == false)
	{
		Debug("Network: not changed.\n");
	}
	else
	{
		Debug("Network: Changed.\n");
	}

#endif

	// Get the current routing table
	table = GetRouteTable();

	// Restore the routing table which has been removed so far
	while (e = GetNext(t->DeletedDefaultGateway))
	{
		bool restore = true;
		UINT i;
		// If the restoring routing entry is a default gateway and
		// the existing routing table contains another default gateway
		// on the interface, give up restoring the entry
		if (IsZeroIP(&e->DestIP) && IsZeroIP(&e->DestMask))
		{
			for (i = 0;i < table->NumEntry;i++)
			{
				ROUTE_ENTRY *r = table->Entry[i];
				if (IsZeroIP(&r->DestIP) && IsZeroIP(&r->DestMask))
				{
					if (r->InterfaceID == e->InterfaceID)
					{
						restore = false;
					}
				}
			}
			if (network_has_changed)
			{
				restore = false;
			}
		}

		if (restore)
		{
			// Routing table restoration
			AddRouteEntry(e);
		}

		// Memory release
		FreeRouteEntry(e);
	}

	// Release
	FreeRouteTable(table);
	ReleaseQueue(t->DeletedDefaultGateway);

	FreeRouteChange(t->RouteChange);

	Free(t);
}

// Write a packet to the virtual LAN card
bool VLanPutPacket(VLAN *v, void *buf, UINT size)
{
	UINT ret;
	// Validate arguments
	if (v == NULL)
	{
		return false;
	}
	if (v->Halt)
	{
		return false;
	}
	if (size > MAX_PACKET_SIZE)
	{
		return false;
	}
	if (buf == NULL || size == 0)
	{
		if (buf != NULL)
		{
			Free(buf);
		}
		return true;
	}

	ret = write(v->fd, buf, size);

	if (ret >= 1)
	{
		Free(buf);
		return true;
	}

	if (errno == EAGAIN || ret == 0)
	{
		Free(buf);
		return true;
	}

	return false;
}

// Get the next packet from the virtual LAN card
bool VLanGetNextPacket(VLAN *v, void **buf, UINT *size)
{
	UCHAR tmp[TAP_READ_BUF_SIZE];
	int ret;
	// Validate arguments
	if (v == NULL || buf == NULL || size == 0)
	{
		return false;
	}
	if (v->Halt)
	{
		return false;
	}

	// Read
	ret = read(v->fd, tmp, sizeof(tmp));

	if (ret == 0 ||
		(ret == -1 && errno == EAGAIN))
	{
		// No packet
		*buf = NULL;
		*size = 0;
		return true;
	}
	else if (ret == -1 || ret > TAP_READ_BUF_SIZE)
	{
		// Error
		return false;
	}
	else
	{
		// Reading packet success
		*buf = Malloc(ret);
		Copy(*buf, tmp, ret);
		*size = ret;
		return true;
	}
}

// Get the cancel object
CANCEL *VLanGetCancel(VLAN *v)
{
	CANCEL *c;
	int fd;
	int yes = 0;
	// Validate arguments
	if (v == NULL)
	{
		return NULL;
	}

	c = NewCancel();
	UnixDeletePipe(c->pipe_read, c->pipe_write);
	c->pipe_read = c->pipe_write = -1;

	fd = v->fd;

	UnixSetSocketNonBlockingMode(fd, true);

	c->SpecialFlag = true;
	c->pipe_read = fd;

	return c;
}

// Close the Virtual LAN card
void FreeVLan(VLAN *v)
{
	// Validate arguments
	if (v == NULL)
	{
		return;
	}

	Free(v->InstanceName);

	Free(v);
}

// Create a tap
VLAN *NewBridgeTap(char *name, char *mac_address, bool create_up)
{
	int fd;
	VLAN *v;
	// Validate arguments
	if (name == NULL || mac_address == NULL)
	{
		return NULL;
	}

	fd = UnixCreateTapDeviceEx(name, UNIX_VLAN_BRIDGE_IFACE_PREFIX, mac_address, create_up);
	if (fd == -1)
	{
		return NULL;
	}

	v = ZeroMalloc(sizeof(VLAN));
	v->Halt = false;
	v->InstanceName = CopyStr(name);
	v->fd = fd;

	return v;
}

// Close the tap
void FreeBridgeTap(VLAN *v)
{
	// Validate arguments
	if (v == NULL)
	{
		return;
	}

	UnixCloseTapDevice(v->fd);
#ifdef	UNIX_BSD
	UnixDestroyBridgeTapDevice(v->InstanceName);
#endif

	FreeVLan(v);
}

// Get the Virtual LAN card list
VLAN *NewVLan(char *instance_name, VLAN_PARAM *param)
{
	int fd;
	VLAN *v;
	// Validate arguments
	if (instance_name == NULL)
	{
		return NULL;
	}

	// Open the tap
	fd = UnixVLanGet(instance_name);
	if (fd == -1)
	{
		return NULL;
	}

	v = ZeroMalloc(sizeof(VLAN));
	v->Halt = false;
	v->InstanceName = CopyStr(instance_name);
	v->fd = fd;

	return v;
}

// Generate TUN interface name
void GenerateTunName(char *name, char *prefix, char *tun_name, size_t tun_name_len)
{
	char instance_name_lower[MAX_SIZE];

	// Generate the device name
	StrCpy(instance_name_lower, sizeof(instance_name_lower), name);
	Trim(instance_name_lower);
	StrLower(instance_name_lower);
	Format(tun_name, tun_name_len, "%s_%s", prefix, instance_name_lower);

	tun_name[15] = 0;
}
// Create a tap device
int UnixCreateTapDeviceEx(char *name, char *prefix, UCHAR *mac_address, bool create_up)
{
	int fd = -1, s = -1;
	char tap_name[MAX_SIZE], tap_path[MAX_SIZE];
	struct ifreq ifr;

	// Validate arguments
	if (name == NULL)
	{
		return -1;
	}

	GenerateTunName(name, prefix, tap_name, sizeof(tap_name));

	// Open the tun / tap
#ifndef	UNIX_BSD
	if (GetOsInfo()->OsType == OSTYPE_LINUX)
	{
		// Linux
		if (IsFile(TAP_FILENAME_1) == false)
		{
			char tmp[MAX_SIZE];

			Format(tmp, sizeof(tmp), "%s c 10 200", TAP_FILENAME_1);
			Run("mknod", tmp, true, true);

			Format(tmp, sizeof(tmp), "600 %s", TAP_FILENAME_1);
			Run("chmod", tmp, true, true);
		}
	}

	fd = open(TAP_FILENAME_1, O_RDWR);
	if (fd == -1)
	{
		// Failure
		fd = open(TAP_FILENAME_2, O_RDWR);
		if (fd == -1)
		{
			return -1;
		}
	}
#else	// UNIX_BSD
	{
		sprintf(tap_path, "%s", TAP_DIR TAP_NAME);
		for (int i = 0; i < TAP_MAX; i++) {
			sprintf(tap_path + StrLen(TAP_DIR TAP_NAME), "%d", i);
			fd = open(tap_path, O_RDWR);
			if (fd != -1)
			{
				break;
			}
		}

		if (fd == -1)
		{
			return -1;
		}
	}
#endif	// UNIX_BSD

#ifdef	UNIX_LINUX
	// Create a TAP device for Linux

	// Set the name and the flags
	Zero(&ifr, sizeof(ifr));
	StrCpy(ifr.ifr_name, sizeof(ifr.ifr_name), tap_name);
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

	if (ioctl(fd, TUNSETIFF, &ifr) == -1)
	{
		// Failure
		close(fd);
		return -1;
	}

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s != -1)
	{
		// Set the MAC address
		if (mac_address != NULL)
		{
			Zero(&ifr, sizeof(ifr));
			StrCpy(ifr.ifr_name, sizeof(ifr.ifr_name), tap_name);
			ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
			Copy(&ifr.ifr_hwaddr.sa_data, mac_address, ETHER_ADDR_LEN);
			ioctl(s, SIOCSIFHWADDR, &ifr);
		}

		if (create_up)
		{
			Zero(&ifr, sizeof(ifr));
			StrCpy(ifr.ifr_name, sizeof(ifr.ifr_name), tap_name);
			ioctl(s, SIOCGIFFLAGS, &ifr);
			ifr.ifr_flags |= IFF_UP;
			ioctl(s, SIOCSIFFLAGS, &ifr);
		}

		close(s);
	}
#endif	// UNIX_LINUX

#ifdef	UNIX_BSD
	// Create a TAP device for BSD
	Zero(&ifr, sizeof(ifr));

	// Get the current name
	StrCpy(ifr.ifr_name, sizeof(ifr.ifr_name), tap_path + StrLen(TAP_DIR));

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s != -1)
	{
		// Set the name, if possible
#ifdef	SIOCSIFNAME
		ifr.ifr_data = tap_name;
		ioctl(s, SIOCSIFNAME, &ifr);
#else	// SIOCSIFNAME
		StrCpy(tap_name, sizeof(tap_name), ifr.ifr_name);
#endif	// SIOCSIFNAME

		// Set the MAC address
		if (mac_address != NULL)
		{
			Zero(&ifr, sizeof(ifr));
			StrCpy(ifr.ifr_name, sizeof(ifr.ifr_name), tap_name);
			ifr.ifr_addr.sa_len = ETHER_ADDR_LEN;
			ifr.ifr_addr.sa_family = AF_LINK;
			Copy(&ifr.ifr_addr.sa_data, mac_address, ETHER_ADDR_LEN);
			ioctl(s, SIOCSIFLLADDR, &ifr);
		}

		// Set interface description
#ifdef	SIOCSIFDESCR
		{
			char desc[] = CEDAR_PRODUCT_STR " Virtual Network Adapter";

			ifr.ifr_buffer.buffer = desc;
			ifr.ifr_buffer.length = StrLen(desc) + 1;
			ioctl(s, SIOCSIFDESCR, &ifr);
		}
#endif

		// Set interface group
		UnixSetIfGroup(s, tap_name, CEDAR_PRODUCT_STR);

		if (create_up)
		{
			Zero(&ifr, sizeof(ifr));
			StrCpy(ifr.ifr_name, sizeof(ifr.ifr_name), tap_name);
			ioctl(s, SIOCGIFFLAGS, &ifr);
			ifr.ifr_flags |= IFF_UP;
			ioctl(s, SIOCSIFFLAGS, &ifr);
		}

		close(s);
	}
#endif	// UNIX_BSD

#ifdef	UNIX_SOLARIS
	// Create a TAP device for Solaris
	{
		int ip_fd;
		int tun_fd;
		int ppa;

		tun_fd = open(tap_name, O_RDWR);
		if (tun_fd == -1)
		{
			// Failure
			close(fd);
			return -1;
		}

		ip_fd = open("/dev/ip", O_RDWR);
		if (ip_fd == -1)
		{
			// Failure
			close(tun_fd);
			close(fd);
			return -1;
		}

		ppa = -1;
		ppa = ioctl(tun_fd, TUNNEWPPA, ppa);
		if (ppa == -1)
		{
			// Failure
			close(tun_fd);
			close(fd);
			close(ip_fd);
			return -1;
		}

		if (ioctl(fd, I_PUSH, "ip") < 0)
		{
			// Failure
			close(tun_fd);
			close(fd);
			close(ip_fd);
			return -1;
		}

		if (ioctl(fd, IF_UNITSEL, (char *)&ppa) < 0)
		{
			// Failure
			close(tun_fd);
			close(fd);
			close(ip_fd);
			return -1;
		}

		if (ioctl(ip_fd, I_LINK, fd) < 0)
		{
			// Failure
			close(tun_fd);
			close(fd);
			close(ip_fd);
			return -1;
		}

		close(tun_fd);
		close(ip_fd);
	}
#endif	// UNIX_SOLARIS

	return fd;
}
int UnixCreateTapDevice(char *name, UCHAR *mac_address, bool create_up)
{
	return UnixCreateTapDeviceEx(name, UNIX_VLAN_CLIENT_IFACE_PREFIX, mac_address, create_up);
}

// Close the tap device
void UnixCloseTapDevice(int fd)
{
	// Validate arguments
	if (fd == -1)
	{
		return;
	}

	close(fd);
}

// Destroy the tap device (for FreeBSD)
// FreeBSD tap device is still plumbed after closing fd so need to destroy after close
void UnixDestroyTapDeviceEx(char *name, char *prefix)
{
#ifdef UNIX_BSD
	struct ifreq ifr;
	char eth_name[MAX_SIZE];
	int s;

	Zero(&ifr, sizeof(ifr));
	GenerateTunName(name, prefix, eth_name, sizeof(eth_name));
	StrCpy(ifr.ifr_name, sizeof(ifr.ifr_name), eth_name);

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == -1)
	{
		return;
	}
	ioctl(s, SIOCIFDESTROY, &ifr);

	close(s);
#endif	// UNIX_BSD
}

void UnixDestroyBridgeTapDevice(char *name)
{
#ifdef UNIX_BSD
	UnixDestroyTapDeviceEx(name, UNIX_VLAN_BRIDGE_IFACE_PREFIX);
#endif	// UNIX_BSD
}

void UnixDestroyClientTapDevice(char *name)
{
#ifdef UNIX_BSD
	UnixDestroyTapDeviceEx(name, UNIX_VLAN_CLIENT_IFACE_PREFIX);
#endif	// UNIX_BSD
}

void UnixSetIfGroup(int fd, const char *name, const char *group_name)
{
#ifdef	SIOCAIFGROUP
	struct ifgroupreq ifgr;
	char *tmp;

	tmp = CopyStr((char *)group_name);
	StrLower(tmp);
	Zero(&ifgr, sizeof(ifgr));

	StrCpy(ifgr.ifgr_name, sizeof(ifgr.ifgr_name), (char *) name);
	StrCpy(ifgr.ifgr_group, sizeof(ifgr.ifgr_group), tmp);
	ioctl(fd, SIOCAIFGROUP, &ifgr);

	Free(tmp);
#endif
}

#else	// NO_VLAN

void UnixCloseDevice(int fd)
{
}

void UnixDestroyTapDevice(char *name)
{
}

void UnixDestroyTapDeviceEx(char *name, char *prefix)
{
}

void UnixSetIfGroup()
{
}

int UnixCreateTapDeviceEx(char *name, char *prefix, UCHAR *mac_address, bool create_up)
{
	return -1;
}
int UnixCreateTapDevice(char *name, UCHAR *mac_address, bool create_up)
{
	return -1;
}

#endif	// NO_VLAN

// Comparison of the VLAN list entries
int UnixCompareVLan(void *p1, void *p2)
{
	UNIX_VLAN_LIST *v1, *v2;
	if (p1 == NULL || p2 == NULL)
	{
		return 0;
	}
	v1 = *(UNIX_VLAN_LIST **)p1;
	v2 = *(UNIX_VLAN_LIST **)p2;
	if (v1 == NULL || v2 == NULL)
	{
		return 0;
	}

	return StrCmpi(v1->Name, v2->Name);
}

// Initialize the VLAN list
void UnixVLanInit()
{
	unix_vlan = NewList(UnixCompareVLan);
}

// Create a VLAN
bool UnixVLanCreateEx(char *name, char *prefix, UCHAR *mac_address, bool create_up)
{
	// Validate arguments
	char tmp[MAX_SIZE];
	if (name == NULL)
	{
		return false;
	}

	StrCpy(tmp, sizeof(tmp), name);
	Trim(tmp);
	name = tmp;

	LockList(unix_vlan);
	{
		UNIX_VLAN_LIST *t, tt;
		int fd;

		// Check whether a device with the same name exists
		Zero(&tt, sizeof(tt));
		StrCpy(tt.Name, sizeof(tt.Name), name);

		t = Search(unix_vlan, &tt);
		if (t != NULL)
		{
			// Already exist
			UnlockList(unix_vlan);
			return false;
		}

		// Create a tap device
		fd = UnixCreateTapDeviceEx(name, prefix, mac_address, create_up);
		if (fd == -1)
		{
			// Failure to create
			UnlockList(unix_vlan);
			return false;
		}

		t = ZeroMalloc(sizeof(UNIX_VLAN_LIST));
		t->fd = fd;
		StrCpy(t->Name, sizeof(t->Name), name);

		Insert(unix_vlan, t);
	}
	UnlockList(unix_vlan);

	return true;
}
bool UnixVLanCreate(char *name, UCHAR *mac_address, bool create_up)
{
	return UnixVLanCreateEx(name, UNIX_VLAN_CLIENT_IFACE_PREFIX, mac_address, create_up);
}

// Set a VLAN up/down
bool UnixVLanSetState(char* name, bool state_up)
{
#if defined(UNIX_LINUX) || defined(UNIX_BSD)
	UNIX_VLAN_LIST *t, tt;
	struct ifreq ifr;
	int s;
	char eth_name[MAX_SIZE];

	LockList(unix_vlan);
	{
		int result;
		// Find a device with the same name
		Zero(&tt, sizeof(tt));
		StrCpy(tt.Name, sizeof(tt.Name), name);

		t = Search(unix_vlan, &tt);
		if (t == NULL)
		{
			// No such device
			UnlockList(unix_vlan);
			return false;
		}

		GenerateTunName(name, UNIX_VLAN_CLIENT_IFACE_PREFIX, eth_name, sizeof(eth_name));
		Zero(&ifr, sizeof(ifr));
		StrCpy(ifr.ifr_name, sizeof(ifr.ifr_name), eth_name);

		s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s == -1)
		{
			// Failed to create socket
			UnlockList(unix_vlan);
			return false;
		}

		ioctl(s, SIOCGIFFLAGS, &ifr);
		if (state_up)
		{
			ifr.ifr_flags |= IFF_UP;
		}
		else
		{
			ifr.ifr_flags &= ~IFF_UP;
		}
		result = ioctl(s, SIOCSIFFLAGS, &ifr);
		close(s);
	}
	UnlockList(unix_vlan);
#endif // UNIX_LINUX || UNIX_BSD

	return true;
}

// Enumerate VLANs
TOKEN_LIST *UnixVLanEnum()
{
	TOKEN_LIST *ret;
	UINT i;
	if (unix_vlan == NULL)
	{
		return NullToken();
	}

	ret = ZeroMalloc(sizeof(TOKEN_LIST));

	LockList(unix_vlan);
	{
		ret->NumTokens = LIST_NUM(unix_vlan);
		ret->Token = ZeroMalloc(sizeof(char *) * ret->NumTokens);

		for (i = 0;i < ret->NumTokens;i++)
		{
			UNIX_VLAN_LIST *t = LIST_DATA(unix_vlan, i);

			ret->Token[i] = CopyStr(t->Name);
		}
	}
	UnlockList(unix_vlan);

	return ret;
}

// Delete the VLAN
void UnixVLanDelete(char *name)
{
	// Validate arguments
	if (name == NULL || unix_vlan == NULL)
	{
		return;
	}

	LockList(unix_vlan);
	{
		UINT i;
		UNIX_VLAN_LIST *t, tt;

		Zero(&tt, sizeof(tt));
		StrCpy(tt.Name, sizeof(tt.Name), name);

		t = Search(unix_vlan, &tt);
		if (t != NULL)
		{
			UnixCloseTapDevice(t->fd);
#ifdef UNIX_BSD
			UnixDestroyClientTapDevice(t->Name);
#endif
			Delete(unix_vlan, t);
			Free(t);
		}
	}
	UnlockList(unix_vlan);
}

// Get the VLAN
int UnixVLanGet(char *name)
{
	int fd = -1;
	// Validate arguments
	if (name == NULL || unix_vlan == NULL)
	{
		return -1;
	}

	LockList(unix_vlan);
	{
		UINT i;
		UNIX_VLAN_LIST *t, tt;

		Zero(&tt, sizeof(tt));
		StrCpy(tt.Name, sizeof(tt.Name), name);

		t = Search(unix_vlan, &tt);
		if (t != NULL)
		{
			fd = t->fd;
		}
	}
	UnlockList(unix_vlan);

	return fd;
}

// Release the VLAN list
void UnixVLanFree()
{
	UINT i;

	for (i = 0;i < LIST_NUM(unix_vlan);i++)
	{
		UNIX_VLAN_LIST *t = LIST_DATA(unix_vlan, i);

		UnixCloseTapDevice(t->fd);
#ifdef UNIX_BSD
		UnixDestroyClientTapDevice(t->Name);
#endif
		Free(t);
	}

	ReleaseList(unix_vlan);
	unix_vlan = NULL;
}

#endif
