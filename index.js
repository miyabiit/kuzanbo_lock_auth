'use strict';

require('dotenv').config();
process.env.NODE_ENV = process.env.NODE_ENV || 'development';
//var express = require('express');
//var cors = require('cors');
const fetch = require('node-fetch');
var Obniz = require("obniz");
const { URLSearchParams } = require('url');

//var app = express();
//app.use(cors());

console.log(`device id = ${process.env.OBNIZ_DEVICE_ID}`)
var obniz = new Obniz(process.env.OBNIZ_DEVICE_ID);

//// TODO: API認証
//// ref: https://staging.minpakuin.jp/api/doc/v1/request/swagger
//// GET /reserves?canceled=false&checkin_from=${today}&checkin_to=${today}
//function fetchPasswordsByAPI() {
//  const userId = process.env.PASSWORD_SERVER_USER_ID;
//  const password = process.env.PASSWORD_SERVER_PASSWORD;
//  fetch(url,
//    {
//      method: 'GET',
//      headers: {
//        Authorization: "Basic " + new Buffer(`${userId}:${password}`).toString("base64"),
//      }
//    }
//  )
//    .then((res) => console.log(res))
//    .catch((err) => console.error(err));
//}

//// (login) GET https://staging.minpakuin.jp/host/login
//// (reserve page) GET https://staging.minpakuin.jp/host/reserves?canceled=false&checkin_from=2020-10-27&checkin_to=2020-10-27&order=checkin_asc
//function fetchPasswordsByPage() {
//  const params = new URLSearchParams();
//  params.append('login_id', process.env.PASSWORD_SERVER_USER_ID);
//  params.append('password', process.env.PASSWORD_SERVER_PASSWORD);
//  fetch(process.env.PASSwORD_SERVER_URL + 'login', { method: 'POST', body: params })
//    .then((res) => {
//      console.log(res)
//    })
//    .catch((err) => console.error(err));
//}

console.log('server start')
obniz.onconnect = async function() {
  obniz.plugin.onreceive = (data) => {
    //console.log(data);
    console.log(`onReceive, data = ${data}`);
    // TODO: fetch
    const passwords = ['012345', '000000', '999999'];
    //console.log(passwords);
    for (const password of passwords) {
      let matched = true;
      if (password.length != data.length) {
        continue;
      }
      for (let i = 0; i < data.length; ++i) {
        if (data[i] != password.charCodeAt(i)) {
          matched = false;
          break;
        }
      }
      if (matched) {
        console.log('MATCH')
        obniz.plugin.send(["O".charCodeAt(0)]);
        return;
      }
    }
    console.log('UNMATCH')
    obniz.plugin.send(["N".charCodeAt(0)]);            
  };
};
