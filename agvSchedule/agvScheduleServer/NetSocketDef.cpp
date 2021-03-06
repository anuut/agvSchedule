#include "stdafx.h"

#include <algorithm>
#include "resource.h"
#include "NetSocketDef.h"

#include "agvScheduleServerView.h"
#include "agvScheduleServerDoc.h"
#include "TaskAndCarInfoDlg.h"
#include "TabCarInfoDlg.h"
#include "TabControlDlg.h"

#include "CarDef.h"
#include "TrafficManager.h"
#include "CarManager.h"


#define USER_PRECISION 5	// 用户指定坐标的精度误差
CString g_cstrCarState[] = { _T("未知"), _T("空闲"),
	_T("已分配"), _T("已连接"), _T("取消") };

CListenSocket::CListenSocket(CagvScheduleServerView* pView, CTaskAndCarInfoDlg* pDlg)
	: m_pView(pView), m_pDlg(pDlg)
{

}


void CListenSocket::OnAccept(int nErrorCode)
{
	CClientSocket* pClientSocket = new	CClientSocket(m_pView, this, m_pDlg);
	if (! Accept(*pClientSocket))
	{
		delete pClientSocket;
		pClientSocket = nullptr;
	}

	// 将连接的agv小车加入list
	m_csClientList.Lock();
	m_clientList.AddTail(pClientSocket);
	m_csClientList.Unlock();

	CSocket::OnAccept(nErrorCode);
}



CClientSocket::CClientSocket(CagvScheduleServerView* pView, 
	CListenSocket* pListenSocket, CTaskAndCarInfoDlg* pDlg)
	: m_pView(pView), m_pListenSocket(pListenSocket), m_pDlg(pDlg)
{

}

void CClientSocket::OnReceive(int nErrorCode)
{
	// TODO: 在此添加专用代码和/或调用基类
	char msge1[24];
	ZeroMemory(msge1, 24);
	int recvBytes = Receive(msge1, 24);

	SetMsgE1(msge1);

	// 交通管理（小车状态）
	TrafficMgn();

	// 给dialog发送E1消息
	setDlgInfo();

	// 显示小车位置
	ShowAGV();

	//AsyncSelect();

	CAsyncSocket::OnReceive(nErrorCode);
}


void CClientSocket::SetMsgE1(char* buf)
{
	//const BYTE head[3] = { 0x21, 0x15, 0x41 };  0 1 2
	//const BYTE type[2] = { 0x45, 0x31 }; 3 4
	//BYTE agvno = 0;  5
	//BYTE m1tag = 0;  6
	//BYTE m2tag = 0;  7
	//BYTE m6tag = 0;  8
	//UINT16 curDist = 0;  9 10
	//UINT16 curSec = 0;   11 12  
	//UINT16 curPoint = 0; 13 14  
	//UINT16 agcStatus = 0; 15 16  
	//BYTE agcError = 0;  17
	//BYTE reserve = 0;   18
	//UINT16 curSpeed = 0; 19 20  
	//BYTE moveOrOpt = 0;  21
	//UINT16 curTask = 0;  22  23
	m_e1.agvno = buf[5];
	m_e1.m1tag = buf[6];
	m_e1.m2tag = buf[7];
	m_e1.m6tag = buf[8];
	m_e1.curDist = (buf[10] << 8) | buf[9];
	m_e1.curSec = (unsigned char)buf[11]/*(buf[12] << 8) | buf[11]*/;
	m_e1.curPoint = (buf[14] << 8) | buf[13];
	m_e1.agcStatus = (buf[16] << 8) | buf[15];
	m_e1.agcError = buf[17];
	m_e1.reserve = buf[18];
	m_e1.curSpeed = (buf[20] << 8) | buf[19];
	m_e1.moveOrOpt = buf[21];
	m_e1.curTask = (buf[23] << 8) | buf[22];
}

void CClientSocket::TrafficMgn()
{
	BYTE carno = m_e1.agvno;		// 车号
	UINT16 state = m_e1.agcStatus;  // 状态
	CagvScheduleServerDoc* pDoc = m_pView->GetDocument();
	CTrafficManager* pMgn = pDoc->m_pTrafficMgn;

	pMgn->SetCurCar(carno);
	if (state != m_prevE1.agcStatus) {
		delete pMgn->m_mapCarState[carno];
		pMgn->m_mapCarState[carno] = nullptr;

		pMgn->m_mapCarState[carno] = GetCarState(state);
		pMgn->SetCurCarState();
	}

	//if (!pState) {
	//	pMgn->m_mapCarState[carno] = GetCarState(state);
	//	pMgn->SetCurCarState();
	//}
	//else {
	//	
	//}
	
}

ICarState* CClientSocket::GetCarState(UINT16 state)
{


	unsigned bit = 1;
	if (state == 0 || ((state >> 10 & bit) == 0)) {
		// AGV调度系统未能读到该小车的信息，小车未插入到系统中
		return new CCarUnknown;
	}

	if ((state >> 10 & bit) == 1 && (state >> 11 & bit) == 1) {
		// 车辆没有操作并且是空载的
		return new CCarFree;
	}

	//if (state & bit && state & (bit << 14)) {
	//	// 载货并且在移动
	//	return new CCarAssignmented;
	//}

	if ((state >> 12 & bit) == 1) {
		// 小车处于急停或手动状态
		return new CCarCanceled;
	}

	return nullptr;
}


void CClientSocket::ShowAGV()
{
	auto pDoc = m_pView->GetDocument();
	HBITMAP& hBitmap = pDoc->GetBitmap();
	CDC* pDC = m_pView->GetDC();

	CDC memdc1, memdc2;
	memdc1.CreateCompatibleDC(pDC);
	memdc2.CreateCompatibleDC(pDC);

	// 获取区域大小
	RECT rc;
	m_pView->GetClientRect(&rc);

	CBitmap bitmapSrc, bitmap2;
	bitmapSrc.Attach(pDoc->GetBitmap());
	bitmap2.CreateCompatibleBitmap(pDC, rc.right, rc.bottom);

	memdc1.SelectObject(&bitmap2);	// 画布大小是客户区大小
	memdc2.SelectObject(&bitmapSrc);

	BITMAP bm;
	bitmapSrc.GetBitmap(&bm);

	// 先让memdc1变成view的大小
	memdc1.StretchBlt(0, 0, rc.right, rc.bottom,
		&memdc2, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);

	CObList& clientList = m_pListenSocket->m_clientList;
	POSITION pos = clientList.GetHeadPosition();
	CPoint curAgvXY;
	CString strAgvNo;
	while (pos) { // 将所有的点画出来
		CClientSocket* pClient = (CClientSocket*)clientList.GetNext(pos);
		// 获取小车当前所在点的坐标
		getAgvXY(pClient, curAgvXY);
		// 画一个圆并显示数字
		memdc1.Ellipse(curAgvXY.x - 4, curAgvXY.y - 4, curAgvXY.x + 4, curAgvXY.y + 4);
		strAgvNo.Format(_T("%d"), pClient->m_e1.agvno);
		memdc1.TextOutW(curAgvXY.x, curAgvXY.y, strAgvNo);
	}

	pDC->StretchBlt(0, 0, rc.right, rc.bottom,
		&memdc1, 0, 0, rc.right, rc.bottom, SRCCOPY);

	bitmapSrc.Detach();
}


void CClientSocket::OnSend(int nErrorCode)
{
	// TODO: 在此添加专用代码和/或调用基类
	/*
		点击调度窗口的ok，发送M1,M2,M6消息
		请看CScheduleDlg::OnBnClickedOk()
	*/
	// 

	CAsyncSocket::OnSend(nErrorCode);
}


void CClientSocket::getAgvXY(CClientSocket* pClient, CPoint& pt)
{
	/*
	E1: agv上报 200ms
	45 31
	01 车号
	01 M1消息标签
	01 M2消息标签
	01 M6消息标签
	00 01 当前所走段距离(mm)
	00 01 当前段号
	00 01 当前点号
	00 01 agc状态位
	01 agc错误码
	01 预留
	00 01 当前速度mm/s
	01 移动完成&操作完成
	00 01 当前任务号
	共21字节
	*/
	const Msg_E1& e1 = pClient->m_e1;
	auto pDoc = m_pView->GetDocument();
	auto& mapSideNo = pDoc->m_sideNo;	// 段，段号
	auto& mapPoint = pDoc->m_mapPoint;	// 点号，坐标									
	auto& vecRoute = pDoc->m_vecRoute;	// 行走路线
	
	CPoint curPt;
	BOOL bFind = mapPoint.Lookup(e1.curPoint, curPt); 
	if (!bFind) {
		pt = pClient->m_pt;
		return;
	}
	
	// 如果当前E1不是该套接字的E1
	if (e1.agvno != m_e1.agvno) {
		pt = pClient->m_pt;
		return;
	}

	// 如果是小车初始位置
	if (vecRoute.empty()) {
		pt = curPt;
		m_pt = pt;
		return;
	}
	
	UINT16 nxPtNo; // 下一个点号
	auto curPtIter = std::find(vecRoute.begin(), vecRoute.end(), e1.curPoint);
	std::advance(curPtIter, 1);
	if (curPtIter == vecRoute.end()) {
		pt = curPt;
		m_pt = pt;
		return;
	}
		

	nxPtNo = (UINT16)*curPtIter;
	CPoint nxPt; // 下一个点号的坐标
	mapPoint.Lookup(nxPtNo, nxPt);	

	// 判断是x向还是y向
	bool bXY = false;	// y向
	bool bPositive = false; // 正方向
	if (fabs(nxPt.y - curPt.y) < USER_PRECISION) {
		bXY = true;		// x向
		// 判断是正方向还是反方向
		if (curPt.x < nxPt.x)
			bPositive = true;
	}
	else {
		if (curPt.y < nxPt.y)
			bPositive = true;
	}
		

	// 计算两点距离 pix（换算成mm）
	// 将E1消息的当前距离换算成屏幕比例 5次 1mm  1-2 57pix 
	int curDist = e1.curDist * 15;
	
	// 计算当前点
	if (bXY) {
		bPositive ? pt.x = curPt.x + curDist : pt.x = curPt.x - curDist;
		pt.y = curPt.y;
	}
	else {
		bPositive ? pt.y = curPt.y + curDist : pt.y = curPt.y - curDist;
		pt.x = curPt.x;	
	}

	pClient->m_pt = pt;
}


void CClientSocket::setDlgInfo()
{
	/*
	E1: agv上报 200ms
	45 31
	01 车号
	01 M1消息标签
	01 M2消息标签
	01 M6消息标签
	00 01 当前所走段距离(mm)
	00 01 当前段号
	00 01 当前点号
	00 01 agc状态位
	01 agc错误码
	01 预留
	00 01 当前速度mm/s
	01 移动完成&操作完成
	00 01 当前任务号
	共21字节
	*/
	// 车辆信息
	static bool bfirst = true;
	if (bfirst) {
		m_prevE1 = m_e1;
		bfirst = false;
	}
	else if (m_e1.curPoint == m_prevE1.curPoint &&
		m_e1.agcStatus == m_prevE1.agcStatus && 
		m_e1.curSec == m_prevE1.curSec) {
		return;
	}

	CListCtrl& ctl = m_pDlg->m_car->m_ctl;
	int nCount = ctl.GetItemCount();
	CString carno, status, curpt, tarpt, curside;
	carno.Format(_T("%d"), m_e1.agvno);

	CTrafficManager* pMgn = m_pView->GetDocument()->m_pTrafficMgn;

	status.Format(_T("%s"), g_cstrCarState[pMgn->m_curState]);
	curpt.Format(_T("%d"), m_e1.curPoint);
	tarpt.Format(_T("%d"), m_targetPt);
	curside.Format(_T("%d"), m_e1.curSec);

	ctl.InsertItem(nCount, carno);
	ctl.SetItemText(nCount, 1, status);
	ctl.SetItemText(nCount, 2, curpt);
	ctl.SetItemText(nCount, 3, tarpt);
	ctl.SetItemText(nCount, 4, curside);

	// 控制面板
	CTabControlDlg* pControl = m_pDlg->m_control;
	unsigned bitMove = 1;
	for (int i = 0; i < 16; ++i) {
		if (m_e1.agcStatus & (bitMove << i)) {
			CStatic* pSts = pControl->m_arrStatus[i];
			pSts->ModifyStyle(0, SS_BITMAP);
			pSts->SetBitmap(LoadBitmap(
				AfxGetInstanceHandle(), MAKEINTRESOURCE(IDB_BITMAP_GREEN)));
		}
	}

	m_prevE1 = m_e1;
}