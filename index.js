'use strict';

require('dotenv').config();
const fs = require("fs");
const puppeteer = require('puppeteer');
const moment = require('moment');

process.env.NODE_ENV = process.env.NODE_ENV || 'development';
//var express = require('express');
//var cors = require('cors');
const fetch = require('node-fetch');
var Obniz = require("obniz");
const { URLSearchParams } = require('url');

console.log(`device id = ${process.env.OBNIZ_DEVICE_ID}`)
var obniz = new Obniz(process.env.OBNIZ_DEVICE_ID, {reset_obniz_on_ws_disconnection: false});

let passwords = [];

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

// (login) GET https://staging.minpakuin.jp/host/login
// (reserve page) GET https://staging.minpakuin.jp/host/reserves?canceled=false&checkin_from=2020-10-27&checkin_to=2020-10-27&order=checkin_asc
function fetchPasswordsByPage() {
  //const params = new URLSearchParams();
  //params.append('login_id', process.env.PASSWORD_SERVER_USER_ID);
  //params.append('password', process.env.PASSWORD_SERVER_PASSWORD);
  //fetch(process.env.PASSWORD_SERVER_URL + 'login', { method: 'POST', body: params })
  //  .then((res) => {
  //    console.log(res)
  //  })
  //  .catch((err) => console.error(err));


  (async () => {
    const browser = await puppeteer.launch();
    const page = await browser.newPage();
    await page.goto(process.env.PASSWORD_SERVER_URL + 'login');
    await page.type('input[name="login_id"]', process.env.PASSWORD_SERVER_USER_ID);
    await page.type('input[name="password"]', process.env.PASSWORD_SERVER_PASSWORD);
    page.click('button[type="submit"]');
    await page.waitForNavigation({ waitUntil: 'domcontentloaded' });

    const useJsonApi = true;
    if (useJsonApi) {
      const apiUrl = process.env.PASSWORD_SERVER_URL + 'reserve-api/list'
      await page.setRequestInterception(true);
      let targetDate = moment().subtract(7, 'days').format("YYYY-MM-DD"); // とりあえずチェックイン日が一週間前まで有効
      let pageNumber = 1;
      let pageCount = null

      const fetchPasswords = async (targetDate, pageNumber) => {
        page.removeAllListeners('request');
        page.on('request', request => {
          //console.log(`${request.url()} = ${apiUrl}`);
          const overrides = {};
          if (request.url() === apiUrl) {
            overrides.method = 'POST';
            overrides.headers = {
              'Content-Type': 'application/json; charset=UTF-8',
            };
            // checkout_from パラメータがない
            //overrides.postData = `{"checkout_from": "${targetDate}", "order": "checkin_asc", "page": ${pageNumber}}`;
            overrides.postData = `{"checkin_from": "${targetDate}", "order": "checkin_asc", "page": ${pageNumber}}`;
          }
          request.continue(overrides);
        });

        await page.goto(apiUrl);

        let content = await page.content(); 
        let json = await page.evaluate(() =>  {
            return JSON.parse(document.querySelector("body").innerText); 
        }); 
        //console.log('fetch by api')
        //console.log(json);
        let passwords = [];
        if (!pageCount) {
          pageCount = Math.ceil(parseInt(json.page_set.total) / parseInt(json.page_set.show_once));
        }
        if (json.reserves) {
          for (const reserve of json.reserves) {
            if (reserve.reserve_key) {
              passwords.push(reserve.reserve_key);
            }
          }
        }
        return passwords;
      };

      let fetchedPasswords = await fetchPasswords(targetDate, pageNumber);

      for (pageNumber = 2; pageNumber <= pageCount; ++pageNumber) {
        fetchedPasswords = fetchedPasswords.concat(await fetchPasswords(targetDate, pageNumber));
      }

      passwords = fetchedPasswords;
    } else {
      await page.goto(process.env.PASSWORD_SERVER_URL + 'reserves');
      await page.waitFor(1000);
      let table_elem = await page.$('.c-table tbody');
      let tr_elems = await table_elem.$$('tr');
      let fetchedPasswords = []
      for (const elem of tr_elems) {
        let td_elems = await elem.$$('td');
        if (td_elems.length > 2) {
          let td_e = td_elems[1];
          fetchedPasswords.push(await td_e.evaluate(e => e.innerText));
        }
      }
      passwords = fetchedPasswords;
    }
    console.log('fetched passwords = ')
    console.log(passwords)

    await browser.close();
  })();
}

fetchPasswordsByPage();

console.log('server start')
obniz.onconnect = async function() {
  obniz.plugin.onreceive = (data) => {
    //console.log(data);
    console.log(`onReceive, data = ${data}`);
    if (data.length > 0 && data[0] == '$'.charCodeAt(0)) { // command
      if (data.length == 5 && data[1] == 'u'.charCodeAt(0) && data[2] == 'p'.charCodeAt(0) && data[3] == 'd'.charCodeAt(0) && data[4] == '?'.charCodeAt(0)) { // receive version-up confirmation
        fs.readFile('command.txt', 'utf-8', (err, cmd_txt) => {
          if (err) {
            console.log('command.txt is not found');
          } else {
            if (cmd_txt.startsWith('update on')) {
              obniz.plugin.send(["U".charCodeAt(0)]);
              console.log('version-up response');
            }
          }
        });
      } else if (data.length == 2 && data[1] == 'f'.charCodeAt(0)) { // fetch command
        fetchPasswordsByPage();
      } else if (data.length == 3 && data[1] == 'a'.charCodeAt(0) && data[2] == '?'.charCodeAt(0)) { // ack
        obniz.plugin.send(["A".charCodeAt(0)]);
        conosole.log('ACK!');
      }
    } else {
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
    }
  };
};
