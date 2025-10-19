function sendMQTT(){
  fetch('/send_mqtt', {method:'POST'})
    .then(r=>r.json())
    .then(j=>{
      if(j.ok) alert('MQTT envoyé !');
      else alert('Échec de l’envoi MQTT.');
    })
    .catch(e=>alert('Erreur de requête MQTT: '+e));
}

let labels=[], measData=[], estData=[], durData=[];
let cuveInitDone = false;

const ctx=document.getElementById('chart').getContext('2d');
const chart=new Chart(ctx,{
  type:'line',
  data:{
    labels:labels,
    datasets:[
      {label:'Mes (cm)',data:measData,borderColor:'blue',fill:false,yAxisID:'y1'},
      {label:'Est (cm)',data:estData,borderColor:'green',fill:false,yAxisID:'y1'},
      {label:'Dur (us)',data:durData,borderColor:'red',fill:false,yAxisID:'y2'}
    ]
  },
  options:{
    responsive:true,
    spanGaps:false,
    animation:false,
    scales:{
      y1:{type:'linear',position:'left'},
      y2:{type:'linear',position:'right'}
    }
  }
});

function refreshDistance(){
  fetch('/distance')
    .then(r=>r.json())
    .then(j=>{
      const m = (j.measured_cm===null)?null:j.measured_cm;
      const e = (j.estimated_cm===null)?null:j.estimated_cm;
      const d = (j.measured_cm===null)?null:j.duration_us;

      document.getElementById('meas').innerText = (m!==null)?m.toFixed(1):'--';
      document.getElementById('est').innerText  = (e!==null && e>-0.5)?e.toFixed(1):'--';
      document.getElementById('dur').innerText  = (d!==null)?d:'--';

      if (!cuveInitDone && typeof j.cuveVide === 'number' && typeof j.cuvePleine === 'number') {
        document.getElementById('v').value = j.cuveVide.toFixed(0);
        document.getElementById('p').value = j.cuvePleine.toFixed(0);
        cuveInitDone = true;
      }

      const t=new Date().toLocaleTimeString();
      labels.push(t);
      if(labels.length>60){labels.shift();measData.shift();estData.shift();durData.shift();}

      measData.push(m !== null ? m : null);
      estData.push(e !== null ? e : null);
      durData.push(d !== null ? d : null);

      chart.update();
    });
}

function refreshCalibs(){
  fetch('/calibs')
    .then(r=>r.json())
    .then(j=>{
      let html='';
      j.calibs.forEach(function(c){
        html += 'C'+(c.index+1)+': Mesuré='+ (c.measured>0?c.measured.toFixed(1):'--') +
                ' Hauteur:<input id="h'+c.index+'" value="'+c.height+'"> ' +
                '<button onclick="save('+c.index+')">Save</button><br>';
      });
      document.getElementById('calibs').innerHTML = html;
    });
}

function save(id){
  const val = document.getElementById('h'+id).value;
  const body = new URLSearchParams({ id: String(id), height: String(val) });
  fetch('/save_calib', {
      method:'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body
  })
    .then(r=>r.json())
    .then(j=>{
      if (j.ok) alert('Saved');
      else alert('Erreur save_calib');
      refreshCalibs();
    })
    .catch(e=>alert('Erreur save_calib: '+e));
}

function saveCuve(){
  const v=document.getElementById('v').value;
  const p=document.getElementById('p').value;
  fetch('/setCuve?vide='+v+'&pleine='+p,{method:'POST'})
    .then(r=>r.json())
    .then(j=>{ if(j.ok) alert('Saved cuve');});
}

function clearCalib(){
  fetch('/clear_calib',{method:'POST'})
    .then(r=>r.json())
    .then(j=>{ alert('Cleared'); refreshCalibs();});
}

function sendPing() {
  fetch('/ping', {
    method: 'POST',
    body: new URLSearchParams({page: 'dashboard'})
  });
}

setInterval(sendPing, 10000);
setInterval(refreshDistance,800);
setInterval(refreshCalibs,5000);
refreshDistance();
refreshCalibs();
