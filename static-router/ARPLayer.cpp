#include "StdAfx.h"
#include "ARPLayer.h"
#include "EthernetLayer.h"
#include "IPLayer.h"
#include "RouterDlg.h"
CARPLayer::CARPLayer(char *pName) : CBaseLayer(pName)
{
	ResetMessage();
	ResetMessageProxy();
	buf_index = 0;
	out_index = 0;	
	buf[0].valid = 0;
	buf[1].valid = 1;
}

CARPLayer::~CARPLayer(void)
{
}

BOOL CARPLayer::Send(unsigned char* ppayload, int nlength,int dev_num)
{
	int index;
	CRouterDlg * routerDlg =  ((CRouterDlg *)(GetUnderLayer()->GetUpperLayer(0)->GetUpperLayer(0)));

	// search 결과 존재할 경우
	if ((index = SearchIpAtTable(routerDlg->m_IPLayer->GetDstIP())) != -1) 
	{
		POSITION pos = Cache_Table.FindIndex(index);
		
		// 해당 결과 : complete
		if (Cache_Table.GetAt(pos).cache_type == complete) 
		{
			((CEthernetLayer *)GetUnderLayer())->SetDestinAddress(Cache_Table.GetAt(pos).Mac_addr);	// 해당 MAC을 설정
			return routerDlg->m_EthernetLayer->Send(ppayload,nlength,ip_type,dev_num);				// 그냥 보냄
		}
		// 해당결과가 incomplete
		else 
		{
			return false;
		}
	}
	// 자신 ip로 보내는 경우
	else if (!memcmp(routerDlg->m_IPLayer->GetDstIP(), routerDlg->m_IPLayer->GetSrcIP(dev_num), 4))
	{
		arp_message.arp_op = request;	// request message
		memcpy(arp_message.arp_srcprotoaddr,routerDlg->m_IPLayer->GetSrcIP(dev_num),4);	// 보내는 사람 ip
		memcpy(arp_message.arp_destprotoaddr,routerDlg->m_IPLayer->GetDstIP(),4);	// 받는사람 ip
		memcpy(arp_message.arp_srchaddr,routerDlg->m_EthernetLayer->GetSourceAddress(dev_num),6);	// 보내는 사람 mac
		return routerDlg->m_EthernetLayer->Send((unsigned char *)&arp_message,ARP_MESSAGE_SIZE,arp_type,dev_num);	// gratuitous arp message
	}
	//serch 결과 존재하지 않을경우
	else
	{
		ResetMessage();	// 메시지 초기화
		// buffer에 패킷 저장
		if (buf[buf_index].valid == 0)	// 버퍼가 비어있을 경우
		{
			buf[buf_index].data = (unsigned char *)malloc(nlength);
			memcpy(buf[buf_index].data,ppayload,nlength);	// 패킷 저장
			buf[buf_index].dev_num = dev_num;
			buf[buf_index].valid = 1;
			buf[buf_index].nlength = nlength;
			memcpy(buf[buf_index].dest_ip,routerDlg->m_IPLayer->GetDstIP(),4);
			buf_index++;
			buf_index %= 2; //Circular 버퍼
		}	// 비어있지 않다면 패킷을 버림

		arp_message.arp_op = request;	// request message
		memcpy(arp_message.arp_srcprotoaddr,routerDlg->m_IPLayer->GetSrcIP(dev_num), 4);	// 보내는 사람 IP
		memcpy(arp_message.arp_srchaddr,routerDlg->m_EthernetLayer->GetSourceAddress(dev_num),6);	// 보내는사람 MAC
		memcpy(arp_message.arp_destprotoaddr,routerDlg->m_IPLayer->GetDstIP(),4);	// 받는사람 IP
		// LP_arpDlg->SetTimer(wait_timer, 4000, NULL);	// Timer 가동
		return routerDlg->m_EthernetLayer->Send((unsigned char *)&arp_message, ARP_MESSAGE_SIZE, arp_type,dev_num);	// ARP Message
	}
	return FALSE;
}

BOOL CARPLayer::Receive(unsigned char* ppayload, int dev_num)
{
	LPARP_Message receive_arp_message = (LPARP_Message)ppayload;
	CRouterDlg * routerDlg =  ((CRouterDlg *)(GetUnderLayer()->GetUpperLayer(0)->GetUpperLayer(0)));
	ResetMessage();
	int index;
	if (receive_arp_message->arp_op == request)	// 요구
	{
		if (memcmp(receive_arp_message->arp_destprotoaddr, routerDlg->m_IPLayer->GetSrcIP(dev_num), 4))	// 내 IP가 아닌데 올 경우
		{
			if ((index = SearchProxyTable(receive_arp_message->arp_destprotoaddr)) != -1)	// Proxy Table에 존재할 경우
			{
				POSITION pos = Proxy_Table.FindIndex(index);
				PROXY_ENTRY entry = Proxy_Table.GetAt(pos);
				proxy_arp_message.arp_op = reply;
				memcpy(proxy_arp_message.arp_desthdaddr,receive_arp_message->arp_srcprotoaddr, 4);
				memcpy(proxy_arp_message.arp_desthdaddr,receive_arp_message->arp_srchaddr, 6);
				memcpy(proxy_arp_message.arp_srchaddr,routerDlg->m_EthernetLayer->GetSourceAddress(dev_num), 6);
				memcpy(proxy_arp_message.arp_srcprotoaddr,entry.Ip_addr,4);	// Proxy 값을 넣고 전송시켜줌
				routerDlg->m_EthernetLayer->Send((unsigned char *)&proxy_arp_message, ARP_MESSAGE_SIZE, arp_type,dev_num);
			}
			// Cache Table에 존재하는지 검사
			else
			{
				if ((index = SearchIpAtTable(receive_arp_message->arp_srcprotoaddr)) != -1)	// Cache Table에 존재할 경우 갱신
				{
					// 해당 Entry를 찾아 값 수정
					POSITION pos = Cache_Table.FindIndex(index);
					Cache_Table.GetAt(pos).cache_ttl = 1200;
					Cache_Table.GetAt(pos).cache_type = complete;
					memcpy(Cache_Table.GetAt(pos).Mac_addr,receive_arp_message->arp_srchaddr,6);
					updateCacheTable();
				}
				// 없을 경우 -- ARP table에 추가
				else
				{
					LPCACHE_ENTRY Cache_entry;
					Cache_entry = (LPCACHE_ENTRY)malloc(sizeof(CACHE_ENTRY));
					memcpy(Cache_entry->Ip_addr,receive_arp_message->arp_srcprotoaddr, 4);
					memcpy(Cache_entry->Mac_addr,receive_arp_message->arp_srchaddr, 6);
					Cache_entry->cache_ttl = 1200;	// 20분
					Cache_entry->cache_type = complete;
					InsertCache(Cache_entry);
				}
			}
		}
		// 자신의 ip로 온 경우
		else
		{
			// destmac == srcmac 일 경우(자기 자신 전송)
			if (!memcmp(receive_arp_message->arp_srchaddr, routerDlg->m_EthernetLayer->GetSourceAddress(dev_num), 6)) 
			{
				return FALSE;
				// 자기자신 전송이므로 무시한다.
			}
			// 아닌 경우
			else 
			{
				arp_message.arp_op = reply;
				memcpy(arp_message.arp_srchaddr, routerDlg->m_EthernetLayer->GetSourceAddress(dev_num), 6);	// 보내는 사람 MAC 주소
				memcpy(arp_message.arp_srcprotoaddr, routerDlg->m_IPLayer->GetSrcIP(dev_num), 4);	// 보내는 사람 IP 주소
				memcpy(arp_message.arp_desthdaddr, receive_arp_message->arp_srchaddr, 6);	// MAC 주소
				memcpy(arp_message.arp_destprotoaddr, receive_arp_message->arp_srcprotoaddr, 4);	// IP 주소
				
				// 자신의 IP와 보내는 쪽 srcIP가 같은 경우 충돌
				if (!memcmp(receive_arp_message->arp_srcprotoaddr, routerDlg->m_IPLayer->GetSrcIP(dev_num), 4))	
				{
					AfxMessageBox(_T("IP충돌 입니다."),0,0);
				}
				else
				{
					if (SearchIpAtTable(receive_arp_message->arp_srcprotoaddr) != -1)
					{
						//table에존재할 경우
					}
					else //테이블에 존재하지 않을경우 해당 ip추가
					{
						//arp_message 설정종료
						LPCACHE_ENTRY Cache_entry; //table 에 insert할 cache
						Cache_entry = (LPCACHE_ENTRY)malloc(sizeof(CACHE_ENTRY));
						memcpy(Cache_entry->Ip_addr,receive_arp_message->arp_srcprotoaddr,4);
						memcpy(Cache_entry->Mac_addr,receive_arp_message->arp_srchaddr,6);
						Cache_entry->cache_ttl = 1200; //20분
						Cache_entry->cache_type = complete;
						InsertCache(Cache_entry);
					}
				}
				//replay메시지를 보내줌
				routerDlg->m_EthernetLayer->Send((unsigned char *)&arp_message,ARP_MESSAGE_SIZE,arp_type,dev_num);
			}
		}
		return TRUE;
	}
	else if(receive_arp_message->arp_op == reply) //응답
	{
		if(!memcmp(receive_arp_message->arp_srcprotoaddr,routerDlg->m_IPLayer->GetSrcIP(dev_num),4)) //자신의 ip = 발송자 ip의경우
		{
			AfxMessageBox("Ip충돌입니다.",0,0);
		}
		// 자신의 IP != 발송자 IP
		else 
		{
			// arp_message 초기화 및 설정
			LPCACHE_ENTRY Cache_entry;	// table 에 insert할 cache
			Cache_entry = (LPCACHE_ENTRY)malloc(sizeof(CACHE_ENTRY));
			memcpy(Cache_entry->Ip_addr,receive_arp_message->arp_srcprotoaddr,4);
			memcpy(Cache_entry->Mac_addr,receive_arp_message->arp_srchaddr,6);
			Cache_entry->cache_ttl = 1200;	// 20분
			Cache_entry->cache_type = complete;
			if ((index = SearchIpAtTable(Cache_entry->Ip_addr)) != -1)	// 존재할 경우 값을 교환
			{
				POSITION pos = Cache_Table.FindIndex(index);
				LPCACHE_ENTRY entry = &Cache_Table.GetAt(pos);
				entry->cache_ttl = 1200;
				entry->cache_type = complete;
				memcpy(entry->Mac_addr,Cache_entry->Mac_addr,6);
				free(Cache_entry);	// 메모리 해제
			}
			else	// 존재하지 않을경우 테이블에 삽입
			{
				InsertCache(Cache_entry);	// cache insert
			}
			if (buf[out_index].valid == 1 && !memcmp(buf[out_index].dest_ip,receive_arp_message->arp_srcprotoaddr, 4))	// 버퍼의 자료 send
			{
				routerDlg->m_EthernetLayer->SetDestinAddress(receive_arp_message->arp_srchaddr);
				routerDlg->m_EthernetLayer->Send(buf[out_index].data,buf[out_index].nlength,ip_type,buf[out_index].dev_num);
				free(buf[out_index].data);
				buf[out_index].valid = 0;
				out_index--;
				out_index %= 2;
			}
			return TRUE;
		}
	}
	return FALSE;
}

int CARPLayer::SearchIpAtTable(unsigned char Ip_addr[4])	// ip 찾는 함수
{
	int count;
	int i;
	int ret = -1;
	CACHE_ENTRY temp;
	if (Cache_Table.GetCount() == 0)	// Cache table 에 아무 것도 없을 경우
	{
		return -1;
	}
	else	// 존재 할 경우
	{
		count = Cache_Table.GetCount();
		for (i=0; i<count; i++)
		{
			temp = Cache_Table.GetAt(Cache_Table.FindIndex(i));
			if (memcmp(Ip_addr, temp.Ip_addr, 4) == 0)
			{
				ret = i;	// index 값 리턴
			}
		}
	}
	return ret;	
}

int CARPLayer::SearchProxyTable(unsigned char Ip_addr[4])
{
	int count;
	int i;
	PROXY_ENTRY temp;
	if (Proxy_Table.GetCount() == 0)	// Proxy table 에 아무것도 없을 경우
	{
		return -1;
	}
	else // 존재 할 경우
	{
		count = Proxy_Table.GetCount();
		for (i=0; i<count; i++)
		{
			temp = Proxy_Table.GetAt(Proxy_Table.FindIndex(i));
			if(memcmp(Ip_addr, temp.Ip_addr,  4) == 0)
			{
				return i;	// index 값 리턴
			}
		}
	}
	return -1;	
}

BOOL CARPLayer::InsertCache(LPCACHE_ENTRY Cache_entry)
{
	Cache_Table.AddTail(*Cache_entry);
	this->updateCacheTable();
	free(Cache_entry);
	return TRUE;
}

BOOL CARPLayer::DeleteCache(int index)
{
	// Cache_Table.RemoveAt(index);
	return TRUE;
}

BOOL CARPLayer::DeleteAllCache()
{
	Cache_Table.RemoveAll();
	return TRUE;
}

BOOL CARPLayer::InsertProxy(CString name, unsigned char ip[4], unsigned char mac[6])
{
	PROXY_ENTRY entry;
	entry.Device_name.Format("%s", name);
	memcpy(entry.Ip_addr, ip, 4);
	memcpy(entry.Mac_addr, mac, 6);
	Proxy_Table.AddTail(entry);
	this->updateProxyTable();
	return TRUE;
}

BOOL CARPLayer::DeleteProxy(int index)
{
	//Proxy_Table.RemoveAt(index);
	return TRUE;
}

BOOL CARPLayer::DeleteAllProxy()
{
	//Proxy_Table.RemoveAll();
	return TRUE;
}


BOOL CARPLayer::ResetMessage()	// 메세지 초기화
{
	arp_message.arp_hdtype = htons(0x0001);
	arp_message.arp_prototype = htons(0x0800);
	arp_message.arp_hdlength = 0x06;
	arp_message.arp_protolength = 0x04;
	arp_message.arp_op = htons(0x0000); //2개로 나뉨
	memset(arp_message.arp_srchaddr, 0, 6);
	memset(arp_message.arp_srcprotoaddr, 0, 4);
	memset(arp_message.arp_destprotoaddr, 0, 4);
	memset(arp_message.arp_desthdaddr, 0, 6);
	return TRUE;
}

BOOL CARPLayer::ResetMessageProxy()
{
	proxy_arp_message.arp_hdtype = htons(0x0001);
	proxy_arp_message.arp_prototype = htons(0x0800);
	proxy_arp_message.arp_hdlength = 0x06;
	proxy_arp_message.arp_protolength = 0x04;
	proxy_arp_message.arp_op = htons(0x0000);	//2개로 나뉨
	memset(proxy_arp_message.arp_srchaddr, 0, 6);
	memset(proxy_arp_message.arp_srcprotoaddr, 0, 4);
	memset(proxy_arp_message.arp_destprotoaddr, 0, 4);
	memset(proxy_arp_message.arp_desthdaddr, 0, 6);
	return TRUE;
}

void CARPLayer::updateCacheTable()	//케쉬 테이블 업데이트 함수
{
	CRouterDlg * routerDlg =  ((CRouterDlg *)(GetUnderLayer()->GetUpperLayer(0)->GetUpperLayer(0)));
	routerDlg->ListBox_ARPCacheTable.DeleteAllItems();	//내용 초기화
	CString ip, mac, time, type;
	POSITION index;
	CACHE_ENTRY entry;	//head position
	for (int i=0; i < Cache_Table.GetCount(); i++)	//캐쉬 테이블 마지막까지
	{
		index = Cache_Table.FindIndex(i);
		entry = Cache_Table.GetAt(index);
		ip.Format("%d.%d.%d.%d", entry.Ip_addr[0], entry.Ip_addr[1], entry.Ip_addr[2], entry.Ip_addr[3]);
		mac.Format("%.02X:%.02X:%.02X:%.02X:%.02X:%.02X", entry.Mac_addr[0], entry.Mac_addr[1], entry.Mac_addr[2], entry.Mac_addr[3], entry.Mac_addr[4], entry.Mac_addr[5]);
		(entry.cache_type == complete ? type.Format("Complete") : type.Format("Incomplete"));
		time.Format("%d:%d", entry.cache_ttl/60,entry.cache_ttl%60);
		routerDlg->ListBox_ARPCacheTable.InsertItem(i, ip);
		routerDlg->ListBox_ARPCacheTable.SetItem(i, 1, LVIF_TEXT,mac,0,0,0,NULL);
		routerDlg->ListBox_ARPCacheTable.SetItem(i, 2, LVIF_TEXT,type,0,0,0,NULL);
		//routerDlg->ListBox_ARPCacheTable.SetItem(i, 3, LVIF_TEXT,time,0,0,0,NULL);
	}
	routerDlg->ListBox_ARPCacheTable.UpdateWindow();
}

void CARPLayer::updateProxyTable()
{		
	CRouterDlg * routerDlg =  ((CRouterDlg *)(GetUnderLayer()->GetUpperLayer(0)->GetUpperLayer(0)));
	routerDlg->ListBox_ARPProxyTable.DeleteAllItems();
	CString ip, mac, name;
	POSITION index1;
	PROXY_ENTRY entry1;	// head position
	for (int i=0; i < Proxy_Table.GetCount(); i++)
	{
		index1 = Proxy_Table.FindIndex(i);
		entry1 = Proxy_Table.GetAt(index1);
		ip.Format("%d.%d.%d.%d", entry1.Ip_addr[0], entry1.Ip_addr[1], entry1.Ip_addr[2], entry1.Ip_addr[3]);
		mac.Format("%.02X:%.02X:%.02X:%.02X:%.02X:%.02X",entry1.Mac_addr[0],entry1.Mac_addr[1],entry1.Mac_addr[2],entry1.Mac_addr[3], entry1.Mac_addr[4],entry1.Mac_addr[5]);
		name.Format("%s",entry1.Device_name);
		routerDlg->ListBox_ARPProxyTable.InsertItem(i,name);
		routerDlg->ListBox_ARPProxyTable.SetItem(i,1,LVIF_TEXT,ip,0,0,0,NULL);
		routerDlg->ListBox_ARPProxyTable.SetItem(i,2,LVIF_TEXT,mac,0,0,0,NULL);
	}
	routerDlg->ListBox_ARPProxyTable.UpdateWindow();
}


void CARPLayer::decreaseTime()
{
	if (!Cache_Table.IsEmpty())
	{
		POSITION index;
		for (int i = 0; i<Cache_Table.GetCount(); i++)
		{
			index = Cache_Table.FindIndex(i);
			unsigned short ttl = Cache_Table.GetAt(index).cache_ttl -= 5; // -5씩 ttl 감소
			if(ttl <= 0) // 삭제
			{
				Cache_Table.RemoveAt(index);
			}
		}
		updateCacheTable();
	}
}

void CARPLayer::ResetCount()
{
	//sendCount = 0;
}

BOOL CARPLayer::reSendMessage()
{
	//CRouterDlg * routerDlg =  ((CRouterDlg *)(GetUnderLayer()->GetUpperLayer(0)->GetUpperLayer(0)));
	//sendCount++;
	//int index;
	//if(sendCount >= 2){ //2번 보낸경우
	//	LPCACHE_ENTRY entry;
	//	entry = (LPCACHE_ENTRY)malloc(sizeof(CACHE_ENTRY));
	//	entry->cache_ttl = 180; //3분 설정
	//	entry->cache_type = incomplete;
	//	memcpy(entry->Ip_addr,arp_message.arp_destprotoaddr,4); //ip coppy
	//	memset(entry->Mac_addr,0,6); //mac 주소를 0으로 초기화
	//	if((index = SearchIpAtTable(entry->Ip_addr)) == -1){ //테이블에 존재하지 않을 경우
	//		InsertCache(entry);
	//	}
	//	else{ //테이블에 존재할 경우
	//		POSITION pos = Cache_Table.FindIndex(index);
	//		Cache_Table.SetAt(pos,*entry);
	//		free(entry);
	//	}
	//	ResetCount(); //count reset
	//	//LP_arpDlg->KillTimer(wait_timer); // kill timer
	//	//LP_arpDlg->resetDestIpEdit();
	//	//LP_arpDlg->SendButtonEnable(TRUE);
	//	return TRUE;
	//}
	//routerDlg->m_EthernetLayer->Send((unsigned char *)&arp_message,ARP_MESSAGE_SIZE,arp_type,dev_num);
	return TRUE;
}