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
      document.getElementById('meas').innerText = j.measured_cm!==null?j.measured_cm.toFixed(1):'--';
      document.getElementById('est').innerText = j.estimated_cm!==null?j.estimated_cm.toFixed(1):'--';
      document.getElementById('dur').innerText = j.duration_us!==null?j.duration_us:'--';
      const t=new Date().toLocaleTimeString();
      labels.push(t);
      if(labels.length>60){labels.shift();measData.shift();estData.shift();durData.shift();}
      measData.push(j.measured_cm||0);
      estData.push(j.estimated_cm||0);
      durData.push(j.duration_us||0);
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
  fetch('/save_calib?id='+id+'&height='+val, {method:'POST'})
    .then(r=>r.json())
    .then(j=>{ if (j.ok) alert('Saved'); refreshCalibs();});
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

setInterval(refreshDistance,800);
setInterval(refreshCalibs,5000);
refreshDistance();
refreshCalibs();
