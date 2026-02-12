// const baseurl = 'http://192.168.4.1'
const baseurl = window.location.origin
console.log(baseurl)
const CONSTANT = {
    wlanName: '',
    GET_STATION_URL: `${baseurl}/system/station_state`,
    GET_POWER_URL: `${baseurl}/system/power_state`,
    POST_CHANGENAME_URL: `${baseurl}/system/station_state/change_name`,
    POST_DELETEDEVICE_URL: `${baseurl}/system/station_state/delete_device`,
}

var getListTime = ''
var batteryTimer = ''
var per = []

var Ajax = {
    get: function(url,callback){
        // XMLHttpRequest对象用于在后台与服务器交换数据
        console.log('4567890')
        var xhr = new XMLHttpRequest();
        xhr.open('GET', url,false);
        xhr.setRequestHeader('Content-Type','application/json')
        // xhr.setRequestHeader('Authorization', 'Basic ZXNwMzI6MTIzNDU2Nzg=')
        xhr.onreadystatechange = function(){
            if(xhr.readyState === 4){
                if(xhr.status === 200 || xhr.status === 304){
                    console.log(xhr.responseText);
                    callback(xhr.responseText);
                } else {
                    console.log(xhr.responseText);
                }
            }
        }
        xhr.send();
    },
    put: function(url, data, callback) {
        var xhr=new XMLHttpRequest();
        xhr.open('put', url,true);
        xhr.setRequestHeader('Content-Type','application/json');
        xhr.onreadystatechange=function(){
            if(xhr.readyState === 4){
                if(xhr.status === 200 || xhr.status === 304){
                    console.log(xhr.responseText);
                    callback(xhr.responseText);
                } else {
                    console.log(xhr.responseText);
                }
            }
        }
        xhr.send(JSON.stringify(data));
    },
    post: function(url, data, callback){
        var xhr=new XMLHttpRequest();
        xhr.open('POST', url,true);
        // 添加http头，发送信息至服务器时内容编码类型
        xhr.setRequestHeader('Content-Type','application/json');
        xhr.onreadystatechange=function(){
            if (xhr.readyState === 4){
                if (xhr.status === 200 || xhr.status === 304){
                    callback(xhr.responseText);
                }
            }
        }
        xhr.send(JSON.stringify(data));
    }
}

function setActiveTab(tab) {
    var one = document.querySelector('.header-title-one')
    var two = document.querySelector('.header-title-two')
    var three = document.querySelector('.header-title-three')
    var network = document.getElementById('networkShow')
    var customer = document.getElementById('customerShow')
    var battery = document.getElementById('batteryShow')

    var tabs = [
        {el: one, view: network, key: 'network'},
        {el: two, view: customer, key: 'clients'},
        {el: three, view: battery, key: 'battery'},
    ]

    tabs.forEach(function (t) {
        var isActive = tab === t.key
        if (t.el) {
            t.el.style.color = isActive ? '#000' : '#888888'
            t.el.style.cursor = isActive ? 'auto' : 'pointer'
        }
        if (t.view) {
            t.view.style.display = isActive ? 'block' : 'none'
        }
    })
}

function networkStatus() {
    setActiveTab('network')
}

function customerList() {
    setActiveTab('clients')
}

function batteryStatus() {
    setActiveTab('battery')
}

function deviceStatusUpdate (nowTime, list, signal) {
    console.log('device Status Update')
    var connectTime = document.getElementById('connect-time')
    var remainCount = document.getElementById('remain-count')
    var connectCount = document.getElementById('connect-count')
    var clientCountTop = document.getElementById('client-count-top')
    var lteSignal = document.getElementById('lte-signal')

    var online_time = nowTime / 1000000;
    var h = Math.floor(online_time / 3600 );
    var m = Math.floor((online_time /60 % 60));
    var s = Math.floor((online_time % 60));

    console.log('list: ', list)
    connectTime.innerHTML = h + 'h ' + m + 'm ' + s + 's'
    remainCount.innerHTML = (10 - list.length) + ''
    if (connectCount) {
        connectCount.innerHTML = list.length + ''
    }

    if (clientCountTop) {
        var val = connectCount ? parseInt(connectCount.innerHTML || '0', 10) : list.length
        clientCountTop.innerHTML = val + ''
        if (val === 0) {
            clientCountTop.classList.add('zero')
        } else {
            clientCountTop.classList.remove('zero')
        }
    }
    if (lteSignal) {
        var hasSignal = signal && typeof signal.dbm === 'number' && signal.dbm > -200 && signal.rssi !== 99
        lteSignal.innerHTML = hasSignal ? signal.dbm + ' dBm' : '– dBm'
    }

}

function createList (list) {
    var html = []
    var tbody = document.getElementById('tbody')
    for(var i = 0;i < list.length; i++){ //遍历一下json数据
        console.log('name: ', list[i].name_str)
        console.log('mac: ', list[i].mac_str)
        console.log('ip:', list[i].ip_str)
        //<td><div data-mac="${list[i].mac_str}" data-value="${list[i].isNetwork}" class="switch-wrap"></div></td>
        var tr = `<tr><td><input value="${list[i].name_str}" disabled class="list-name" data-index="${i}" id="row + ${i}" /></td><td>${list[i].mac_str}</td><td>${list[i].ip_str}</td><td>${list[i].connectTime}</td><td><div class="flex flex-jcc"><input class="edit-btn" type="button" id="edit + ${i}" data-id="row + ${i}" value="Bearbeiten" /><div class="edit-line"></div><input class="del-btn" type="button" data-index="${i}" id="dle + ${i}" value="Löschen"/></div></td></tr>`
        html.push(tr)
    }
    tbody.innerHTML = html.join('')
}

function listDataDelete (index) {
    var deleteDeviceDic = per[index]

    console.log('deleteDeviceDic body: ' + JSON.stringify(deleteDeviceDic))
    Ajax.post(CONSTANT.POST_DELETEDEVICE_URL, deleteDeviceDic, function (res) {
        console.log('listDataDelete block: ' + res)
    })
}

function editNameUpdate (index, value) {
    var stationDic = per[index]
    stationDic['name_str'] = value

    console.log('stationDic body: ' + JSON.stringify(stationDic))
    Ajax.post(CONSTANT.POST_CHANGENAME_URL, stationDic, function (res) {
        console.log('editNameUpdate block: ' + res)
    })
}

function updateRouterList () {
    getListTime = setTimeout(updateRouterList,4000)
    Ajax.get(CONSTANT.GET_STATION_URL, function (res) {
        res = JSON.parse(res)
        console.log('获取基本信息： ', res)
        console.log('获取基本信息： ', res.station_list)
        per = res.station_list
        deviceStatusUpdate(res.now_time, res.station_list, res.signal)
        for (var i = 0; i < per.length; i++) {
            var stationDic = per[i]
            var online_time = (res.now_time - stationDic.online_time_s)/1000000
            var h = Math.floor(online_time / 3600 )
            var m = Math.floor((online_time /60 % 60))
            var s = Math.floor((online_time % 60))
            let connectTime = h + 'h ' + m + 'm ' + s + 's'
            stationDic['connectTime'] = connectTime
            per.splice(i, 1, stationDic)
        }
        console.log('页面上要显示的信息： ', per)

        createList(per)
    })
}

function updateBattery () {
    batteryTimer = setTimeout(updateBattery,15000)
    Ajax.get(CONSTANT.GET_POWER_URL, function (res) {
        res = JSON.parse(res)
        var vEl = document.getElementById('battery-voltage')
        var socEl = document.getElementById('battery-soc')
        var ageEl = document.getElementById('battery-age')

        if (res && res.ok) {
            if (typeof res.voltage === 'number') {
                vEl.innerHTML = res.voltage.toFixed(3) + ' V'
            }
            if (typeof res.percent === 'number') {
                socEl.innerHTML = res.percent.toFixed(1) + ' %'
            }
            var age = (typeof res.age_s === 'number') ? res.age_s : null
            ageEl.innerHTML = (age !== null) ? age.toFixed(1) + ' s' : '–'
        } else {
            vEl.innerHTML = '–'
            socEl.innerHTML = '–'
            ageEl.innerHTML = '–'
        }
    })
}

function menuClick (e) {
    console.log('e: ', e.classList)
    var menuView = document.getElementById('leftMenu')
    if (e.classList.contains('delMenu')) {
        menuView.style.display = 'none'
        e.classList.remove('delMenu')
    } else {
        e.classList.add('delMenu')
        menuView.style.display = 'block'
    }

}

function initHash () {
    console.log('Initialer Seiten-Ladevorgang')

    setActiveTab('network')

    updateRouterList()
    if (!batteryTimer) {
        updateBattery()
    }
    var dom = document.querySelector('table')
    dom.addEventListener('click', function(e) {
        // if (e.target.className === 'switch-wrap') {
        //     console.log(e.target.getAttribute('data-mac'))
        //     if (e.target.getAttribute('data-value') === 'true') {
        //         e.target.setAttribute('data-value', false)
        //     } else {
        //         e.target.setAttribute('data-value', true)
        //     }
        // }
        if (e.target.className === 'edit-btn') {
            console.log('editBtn')
            clearInterval(getListTime)
            var inputId = e.target.getAttribute('id')
            var inputDataId = e.target.getAttribute('data-id')
            var editBtn = document.getElementById(inputId)
            var listName = document.getElementById(inputDataId)
            // var listName = document.getElementById('list-name-id')
            if (editBtn.value === 'Bearbeiten') {
                console.log('Editieren aktiv')
                listName.disabled = ''
                listName.style.border = '1px solid #CCCCCC'
                editBtn.value = 'Speichern'
            } else {
                console.log('Editieren speichern')
                listName.disabled = 'disabled'
                listName.style.border = '0'
                editBtn.value = 'Bearbeiten'
                console.log('listName: ' + listName.value)
                console.log('listName: ' + listName.getAttribute('data-index'))
                editNameUpdate(listName.getAttribute('data-index'), listName.value)
                updateRouterList()
            }
        }

        if (e.target.className === 'del-btn') {
            clearInterval(getListTime)
            console.log('删除')
            var deleteId = e.target.getAttribute('id')
            var deleteBtn = document.getElementById(deleteId)
            var target = e.target
            console.log(target.parentNode.parentNode.parentNode)
            console.log('delete index: ' + deleteBtn.getAttribute('data-index'))

            var msg = 'Gerät löschen?'
            if (confirm(msg) === true) {
                if (e.target.className === 'del-btn') {
                    tbody.removeChild(target.parentNode.parentNode.parentNode)
                    listDataDelete(deleteBtn.getAttribute('data-index'))
                }
                updateRouterList()
            } else {
                console.log('取消删除数据')
                updateRouterList()
            }
        }
    })
    // createList(per)
}
