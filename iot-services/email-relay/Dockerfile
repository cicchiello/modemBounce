FROM python:3.12-alpine

WORKDIR /app
COPY email_relay.py /app/email_relay.py

EXPOSE 8093

CMD ["python", "/app/email_relay.py"]
