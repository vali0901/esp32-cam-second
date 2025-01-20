document.getElementById("tokenForm").addEventListener("submit", async function (e) {
    e.preventDefault();
    const token = document.getElementById("token").value;
    const action = document.getElementById("action").value;

    console.log(token);
    const response = await fetch("/token_mgmt/submit", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ "token": token, "action": action }),
    });

    const result = await response.text();
    document.getElementById("token_response").textContent = result;
});


document.getElementById('back').addEventListener('click', () => {
    fetch('/')
        .then(response => {
            if (response.ok) {
                window.location.href = '/';
            } else {
                alert('Failed to navigate back. Please try again.');
            }
        })
        .catch(error => {
            console.error('Error:', error);
            alert('An error occurred. Please try again.');
        });
});

