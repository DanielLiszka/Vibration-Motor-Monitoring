from typing import Dict


def create_data_routes(data_collector):
    def ingest_sample(data: Dict) -> Dict:
        if not data_collector:
            return {'status': 'error', 'message': 'Data collector not available'}
        if data_collector.receive_sample(data):
            data_collector.flush()
            return {'status': 'ok', 'message': 'Sample accepted'}
        return {'status': 'error', 'message': 'Failed to accept sample'}

    def ingest_batch(data: Dict) -> Dict:
        if not data_collector:
            return {'status': 'error', 'message': 'Data collector not available'}
        count = data_collector.receive_batch(data)
        data_collector.flush()
        return {'status': 'ok', 'count': count}

    def get_unlabeled(limit: int=100, device_id: str=None) -> Dict:
        if not data_collector:
            return {'status': 'ok', 'samples': [], 'count': 0}
        samples = data_collector.get_unlabeled_samples(limit=limit, device_id=device_id)
        return {'status': 'ok', 'samples': samples, 'count': len(samples)}

    def get_summary() -> Dict:
        if not data_collector:
            return {'status': 'error', 'message': 'Data collector not available'}
        return {'status': 'ok', 'summary': data_collector.get_stats_summary()}

    return {
        'ingest_sample': ingest_sample,
        'ingest_batch': ingest_batch,
        'get_unlabeled': get_unlabeled,
        'get_summary': get_summary,
    }


def register_flask_routes(app, data_collector):
    from flask import request, jsonify

    handlers = create_data_routes(data_collector)

    @app.route('/api/data/sample', methods=['POST'])
    def ingest_sample():
        return jsonify(handlers['ingest_sample'](request.json or {}))

    @app.route('/api/data/batch', methods=['POST'])
    def ingest_batch():
        return jsonify(handlers['ingest_batch'](request.json or {}))

    @app.route('/api/data/unlabeled', methods=['GET'])
    def unlabeled():
        device_id = request.args.get('device_id')
        limit = request.args.get('limit', 100, type=int)
        return jsonify(handlers['get_unlabeled'](limit=limit, device_id=device_id))

    @app.route('/api/data/summary', methods=['GET'])
    def summary():
        return jsonify(handlers['get_summary']())
